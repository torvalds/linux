/*
 * Copyright (c) 2004-2007 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2005 Intel Corporation.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2014,2018 Intel Corporation.  All rights reserved.
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/security.h>
#include <linux/xarray.h>
#include <rdma/ib_cache.h>

#include "mad_priv.h"
#include "core_priv.h"
#include "mad_rmpp.h"
#include "smi.h"
#include "opa_smi.h"
#include "agent.h"

#define CREATE_TRACE_POINTS
#include <trace/events/ib_mad.h>

#ifdef CONFIG_TRACEPOINTS
static void create_mad_addr_info(struct ib_mad_send_wr_private *mad_send_wr,
			  struct ib_mad_qp_info *qp_info,
			  struct trace_event_raw_ib_mad_send_template *entry)
{
	u16 pkey;
	struct ib_device *dev = qp_info->port_priv->device;
	u8 pnum = qp_info->port_priv->port_num;
	struct ib_ud_wr *wr = &mad_send_wr->send_wr;
	struct rdma_ah_attr attr = {};

	rdma_query_ah(wr->ah, &attr);

	/* These are common */
	entry->sl = attr.sl;
	ib_query_pkey(dev, pnum, wr->pkey_index, &pkey);
	entry->pkey = pkey;
	entry->rqpn = wr->remote_qpn;
	entry->rqkey = wr->remote_qkey;
	entry->dlid = rdma_ah_get_dlid(&attr);
}
#endif

static int mad_sendq_size = IB_MAD_QP_SEND_SIZE;
static int mad_recvq_size = IB_MAD_QP_RECV_SIZE;

module_param_named(send_queue_size, mad_sendq_size, int, 0444);
MODULE_PARM_DESC(send_queue_size, "Size of send queue in number of work requests");
module_param_named(recv_queue_size, mad_recvq_size, int, 0444);
MODULE_PARM_DESC(recv_queue_size, "Size of receive queue in number of work requests");

static DEFINE_XARRAY_ALLOC1(ib_mad_clients);
static u32 ib_mad_client_next;
static struct list_head ib_mad_port_list;

/* Port list lock */
static DEFINE_SPINLOCK(ib_mad_port_list_lock);

/* Forward declarations */
static int method_in_use(struct ib_mad_mgmt_method_table **method,
			 struct ib_mad_reg_req *mad_reg_req);
static void remove_mad_reg_req(struct ib_mad_agent_private *priv);
static struct ib_mad_agent_private *find_mad_agent(
					struct ib_mad_port_private *port_priv,
					const struct ib_mad_hdr *mad);
static int ib_mad_post_receive_mads(struct ib_mad_qp_info *qp_info,
				    struct ib_mad_private *mad);
static void cancel_mads(struct ib_mad_agent_private *mad_agent_priv);
static void timeout_sends(struct work_struct *work);
static void local_completions(struct work_struct *work);
static int add_nonoui_reg_req(struct ib_mad_reg_req *mad_reg_req,
			      struct ib_mad_agent_private *agent_priv,
			      u8 mgmt_class);
static int add_oui_reg_req(struct ib_mad_reg_req *mad_reg_req,
			   struct ib_mad_agent_private *agent_priv);
static bool ib_mad_send_error(struct ib_mad_port_private *port_priv,
			      struct ib_wc *wc);
static void ib_mad_send_done(struct ib_cq *cq, struct ib_wc *wc);

/*
 * Returns a ib_mad_port_private structure or NULL for a device/port
 * Assumes ib_mad_port_list_lock is being held
 */
static inline struct ib_mad_port_private *
__ib_get_mad_port(struct ib_device *device, int port_num)
{
	struct ib_mad_port_private *entry;

	list_for_each_entry(entry, &ib_mad_port_list, port_list) {
		if (entry->device == device && entry->port_num == port_num)
			return entry;
	}
	return NULL;
}

/*
 * Wrapper function to return a ib_mad_port_private structure or NULL
 * for a device/port
 */
static inline struct ib_mad_port_private *
ib_get_mad_port(struct ib_device *device, int port_num)
{
	struct ib_mad_port_private *entry;
	unsigned long flags;

	spin_lock_irqsave(&ib_mad_port_list_lock, flags);
	entry = __ib_get_mad_port(device, port_num);
	spin_unlock_irqrestore(&ib_mad_port_list_lock, flags);

	return entry;
}

static inline u8 convert_mgmt_class(u8 mgmt_class)
{
	/* Alias IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE to 0 */
	return mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE ?
		0 : mgmt_class;
}

static int get_spl_qp_index(enum ib_qp_type qp_type)
{
	switch (qp_type)
	{
	case IB_QPT_SMI:
		return 0;
	case IB_QPT_GSI:
		return 1;
	default:
		return -1;
	}
}

static int vendor_class_index(u8 mgmt_class)
{
	return mgmt_class - IB_MGMT_CLASS_VENDOR_RANGE2_START;
}

static int is_vendor_class(u8 mgmt_class)
{
	if ((mgmt_class < IB_MGMT_CLASS_VENDOR_RANGE2_START) ||
	    (mgmt_class > IB_MGMT_CLASS_VENDOR_RANGE2_END))
		return 0;
	return 1;
}

static int is_vendor_oui(char *oui)
{
	if (oui[0] || oui[1] || oui[2])
		return 1;
	return 0;
}

static int is_vendor_method_in_use(
		struct ib_mad_mgmt_vendor_class *vendor_class,
		struct ib_mad_reg_req *mad_reg_req)
{
	struct ib_mad_mgmt_method_table *method;
	int i;

	for (i = 0; i < MAX_MGMT_OUI; i++) {
		if (!memcmp(vendor_class->oui[i], mad_reg_req->oui, 3)) {
			method = vendor_class->method_table[i];
			if (method) {
				if (method_in_use(&method, mad_reg_req))
					return 1;
				else
					break;
			}
		}
	}
	return 0;
}

int ib_response_mad(const struct ib_mad_hdr *hdr)
{
	return ((hdr->method & IB_MGMT_METHOD_RESP) ||
		(hdr->method == IB_MGMT_METHOD_TRAP_REPRESS) ||
		((hdr->mgmt_class == IB_MGMT_CLASS_BM) &&
		 (hdr->attr_mod & IB_BM_ATTR_MOD_RESP)));
}
EXPORT_SYMBOL(ib_response_mad);

/*
 * ib_register_mad_agent - Register to send/receive MADs
 *
 * Context: Process context.
 */
struct ib_mad_agent *ib_register_mad_agent(struct ib_device *device,
					   u8 port_num,
					   enum ib_qp_type qp_type,
					   struct ib_mad_reg_req *mad_reg_req,
					   u8 rmpp_version,
					   ib_mad_send_handler send_handler,
					   ib_mad_recv_handler recv_handler,
					   void *context,
					   u32 registration_flags)
{
	struct ib_mad_port_private *port_priv;
	struct ib_mad_agent *ret = ERR_PTR(-EINVAL);
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_reg_req *reg_req = NULL;
	struct ib_mad_mgmt_class_table *class;
	struct ib_mad_mgmt_vendor_class_table *vendor;
	struct ib_mad_mgmt_vendor_class *vendor_class;
	struct ib_mad_mgmt_method_table *method;
	int ret2, qpn;
	u8 mgmt_class, vclass;

	if ((qp_type == IB_QPT_SMI && !rdma_cap_ib_smi(device, port_num)) ||
	    (qp_type == IB_QPT_GSI && !rdma_cap_ib_cm(device, port_num)))
		return ERR_PTR(-EPROTONOSUPPORT);

	/* Validate parameters */
	qpn = get_spl_qp_index(qp_type);
	if (qpn == -1) {
		dev_dbg_ratelimited(&device->dev, "%s: invalid QP Type %d\n",
				    __func__, qp_type);
		goto error1;
	}

	if (rmpp_version && rmpp_version != IB_MGMT_RMPP_VERSION) {
		dev_dbg_ratelimited(&device->dev,
				    "%s: invalid RMPP Version %u\n",
				    __func__, rmpp_version);
		goto error1;
	}

	/* Validate MAD registration request if supplied */
	if (mad_reg_req) {
		if (mad_reg_req->mgmt_class_version >= MAX_MGMT_VERSION) {
			dev_dbg_ratelimited(&device->dev,
					    "%s: invalid Class Version %u\n",
					    __func__,
					    mad_reg_req->mgmt_class_version);
			goto error1;
		}
		if (!recv_handler) {
			dev_dbg_ratelimited(&device->dev,
					    "%s: no recv_handler\n", __func__);
			goto error1;
		}
		if (mad_reg_req->mgmt_class >= MAX_MGMT_CLASS) {
			/*
			 * IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE is the only
			 * one in this range currently allowed
			 */
			if (mad_reg_req->mgmt_class !=
			    IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) {
				dev_dbg_ratelimited(&device->dev,
					"%s: Invalid Mgmt Class 0x%x\n",
					__func__, mad_reg_req->mgmt_class);
				goto error1;
			}
		} else if (mad_reg_req->mgmt_class == 0) {
			/*
			 * Class 0 is reserved in IBA and is used for
			 * aliasing of IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE
			 */
			dev_dbg_ratelimited(&device->dev,
					    "%s: Invalid Mgmt Class 0\n",
					    __func__);
			goto error1;
		} else if (is_vendor_class(mad_reg_req->mgmt_class)) {
			/*
			 * If class is in "new" vendor range,
			 * ensure supplied OUI is not zero
			 */
			if (!is_vendor_oui(mad_reg_req->oui)) {
				dev_dbg_ratelimited(&device->dev,
					"%s: No OUI specified for class 0x%x\n",
					__func__,
					mad_reg_req->mgmt_class);
				goto error1;
			}
		}
		/* Make sure class supplied is consistent with RMPP */
		if (!ib_is_mad_class_rmpp(mad_reg_req->mgmt_class)) {
			if (rmpp_version) {
				dev_dbg_ratelimited(&device->dev,
					"%s: RMPP version for non-RMPP class 0x%x\n",
					__func__, mad_reg_req->mgmt_class);
				goto error1;
			}
		}

		/* Make sure class supplied is consistent with QP type */
		if (qp_type == IB_QPT_SMI) {
			if ((mad_reg_req->mgmt_class !=
					IB_MGMT_CLASS_SUBN_LID_ROUTED) &&
			    (mad_reg_req->mgmt_class !=
					IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)) {
				dev_dbg_ratelimited(&device->dev,
					"%s: Invalid SM QP type: class 0x%x\n",
					__func__, mad_reg_req->mgmt_class);
				goto error1;
			}
		} else {
			if ((mad_reg_req->mgmt_class ==
					IB_MGMT_CLASS_SUBN_LID_ROUTED) ||
			    (mad_reg_req->mgmt_class ==
					IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)) {
				dev_dbg_ratelimited(&device->dev,
					"%s: Invalid GS QP type: class 0x%x\n",
					__func__, mad_reg_req->mgmt_class);
				goto error1;
			}
		}
	} else {
		/* No registration request supplied */
		if (!send_handler)
			goto error1;
		if (registration_flags & IB_MAD_USER_RMPP)
			goto error1;
	}

	/* Validate device and port */
	port_priv = ib_get_mad_port(device, port_num);
	if (!port_priv) {
		dev_dbg_ratelimited(&device->dev, "%s: Invalid port %d\n",
				    __func__, port_num);
		ret = ERR_PTR(-ENODEV);
		goto error1;
	}

	/* Verify the QP requested is supported. For example, Ethernet devices
	 * will not have QP0.
	 */
	if (!port_priv->qp_info[qpn].qp) {
		dev_dbg_ratelimited(&device->dev, "%s: QP %d not supported\n",
				    __func__, qpn);
		ret = ERR_PTR(-EPROTONOSUPPORT);
		goto error1;
	}

	/* Allocate structures */
	mad_agent_priv = kzalloc(sizeof *mad_agent_priv, GFP_KERNEL);
	if (!mad_agent_priv) {
		ret = ERR_PTR(-ENOMEM);
		goto error1;
	}

	if (mad_reg_req) {
		reg_req = kmemdup(mad_reg_req, sizeof *reg_req, GFP_KERNEL);
		if (!reg_req) {
			ret = ERR_PTR(-ENOMEM);
			goto error3;
		}
	}

	/* Now, fill in the various structures */
	mad_agent_priv->qp_info = &port_priv->qp_info[qpn];
	mad_agent_priv->reg_req = reg_req;
	mad_agent_priv->agent.rmpp_version = rmpp_version;
	mad_agent_priv->agent.device = device;
	mad_agent_priv->agent.recv_handler = recv_handler;
	mad_agent_priv->agent.send_handler = send_handler;
	mad_agent_priv->agent.context = context;
	mad_agent_priv->agent.qp = port_priv->qp_info[qpn].qp;
	mad_agent_priv->agent.port_num = port_num;
	mad_agent_priv->agent.flags = registration_flags;
	spin_lock_init(&mad_agent_priv->lock);
	INIT_LIST_HEAD(&mad_agent_priv->send_list);
	INIT_LIST_HEAD(&mad_agent_priv->wait_list);
	INIT_LIST_HEAD(&mad_agent_priv->done_list);
	INIT_LIST_HEAD(&mad_agent_priv->rmpp_list);
	INIT_DELAYED_WORK(&mad_agent_priv->timed_work, timeout_sends);
	INIT_LIST_HEAD(&mad_agent_priv->local_list);
	INIT_WORK(&mad_agent_priv->local_work, local_completions);
	atomic_set(&mad_agent_priv->refcount, 1);
	init_completion(&mad_agent_priv->comp);

	ret2 = ib_mad_agent_security_setup(&mad_agent_priv->agent, qp_type);
	if (ret2) {
		ret = ERR_PTR(ret2);
		goto error4;
	}

	/*
	 * The mlx4 driver uses the top byte to distinguish which virtual
	 * function generated the MAD, so we must avoid using it.
	 */
	ret2 = xa_alloc_cyclic(&ib_mad_clients, &mad_agent_priv->agent.hi_tid,
			mad_agent_priv, XA_LIMIT(0, (1 << 24) - 1),
			&ib_mad_client_next, GFP_KERNEL);
	if (ret2 < 0) {
		ret = ERR_PTR(ret2);
		goto error5;
	}

	/*
	 * Make sure MAD registration (if supplied)
	 * is non overlapping with any existing ones
	 */
	spin_lock_irq(&port_priv->reg_lock);
	if (mad_reg_req) {
		mgmt_class = convert_mgmt_class(mad_reg_req->mgmt_class);
		if (!is_vendor_class(mgmt_class)) {
			class = port_priv->version[mad_reg_req->
						   mgmt_class_version].class;
			if (class) {
				method = class->method_table[mgmt_class];
				if (method) {
					if (method_in_use(&method,
							   mad_reg_req))
						goto error6;
				}
			}
			ret2 = add_nonoui_reg_req(mad_reg_req, mad_agent_priv,
						  mgmt_class);
		} else {
			/* "New" vendor class range */
			vendor = port_priv->version[mad_reg_req->
						    mgmt_class_version].vendor;
			if (vendor) {
				vclass = vendor_class_index(mgmt_class);
				vendor_class = vendor->vendor_class[vclass];
				if (vendor_class) {
					if (is_vendor_method_in_use(
							vendor_class,
							mad_reg_req))
						goto error6;
				}
			}
			ret2 = add_oui_reg_req(mad_reg_req, mad_agent_priv);
		}
		if (ret2) {
			ret = ERR_PTR(ret2);
			goto error6;
		}
	}
	spin_unlock_irq(&port_priv->reg_lock);

	trace_ib_mad_create_agent(mad_agent_priv);
	return &mad_agent_priv->agent;
error6:
	spin_unlock_irq(&port_priv->reg_lock);
	xa_erase(&ib_mad_clients, mad_agent_priv->agent.hi_tid);
error5:
	ib_mad_agent_security_cleanup(&mad_agent_priv->agent);
error4:
	kfree(reg_req);
error3:
	kfree(mad_agent_priv);
error1:
	return ret;
}
EXPORT_SYMBOL(ib_register_mad_agent);

static inline void deref_mad_agent(struct ib_mad_agent_private *mad_agent_priv)
{
	if (atomic_dec_and_test(&mad_agent_priv->refcount))
		complete(&mad_agent_priv->comp);
}

static void unregister_mad_agent(struct ib_mad_agent_private *mad_agent_priv)
{
	struct ib_mad_port_private *port_priv;

	/* Note that we could still be handling received MADs */
	trace_ib_mad_unregister_agent(mad_agent_priv);

	/*
	 * Canceling all sends results in dropping received response
	 * MADs, preventing us from queuing additional work
	 */
	cancel_mads(mad_agent_priv);
	port_priv = mad_agent_priv->qp_info->port_priv;
	cancel_delayed_work(&mad_agent_priv->timed_work);

	spin_lock_irq(&port_priv->reg_lock);
	remove_mad_reg_req(mad_agent_priv);
	spin_unlock_irq(&port_priv->reg_lock);
	xa_erase(&ib_mad_clients, mad_agent_priv->agent.hi_tid);

	flush_workqueue(port_priv->wq);

	deref_mad_agent(mad_agent_priv);
	wait_for_completion(&mad_agent_priv->comp);
	ib_cancel_rmpp_recvs(mad_agent_priv);

	ib_mad_agent_security_cleanup(&mad_agent_priv->agent);

	kfree(mad_agent_priv->reg_req);
	kfree_rcu(mad_agent_priv, rcu);
}

/*
 * ib_unregister_mad_agent - Unregisters a client from using MAD services
 *
 * Context: Process context.
 */
void ib_unregister_mad_agent(struct ib_mad_agent *mad_agent)
{
	struct ib_mad_agent_private *mad_agent_priv;

	mad_agent_priv = container_of(mad_agent,
				      struct ib_mad_agent_private,
				      agent);
	unregister_mad_agent(mad_agent_priv);
}
EXPORT_SYMBOL(ib_unregister_mad_agent);

static void dequeue_mad(struct ib_mad_list_head *mad_list)
{
	struct ib_mad_queue *mad_queue;
	unsigned long flags;

	mad_queue = mad_list->mad_queue;
	spin_lock_irqsave(&mad_queue->lock, flags);
	list_del(&mad_list->list);
	mad_queue->count--;
	spin_unlock_irqrestore(&mad_queue->lock, flags);
}

static void build_smp_wc(struct ib_qp *qp, struct ib_cqe *cqe, u16 slid,
		u16 pkey_index, u8 port_num, struct ib_wc *wc)
{
	memset(wc, 0, sizeof *wc);
	wc->wr_cqe = cqe;
	wc->status = IB_WC_SUCCESS;
	wc->opcode = IB_WC_RECV;
	wc->pkey_index = pkey_index;
	wc->byte_len = sizeof(struct ib_mad) + sizeof(struct ib_grh);
	wc->src_qp = IB_QP0;
	wc->qp = qp;
	wc->slid = slid;
	wc->sl = 0;
	wc->dlid_path_bits = 0;
	wc->port_num = port_num;
}

static size_t mad_priv_size(const struct ib_mad_private *mp)
{
	return sizeof(struct ib_mad_private) + mp->mad_size;
}

static struct ib_mad_private *alloc_mad_private(size_t mad_size, gfp_t flags)
{
	size_t size = sizeof(struct ib_mad_private) + mad_size;
	struct ib_mad_private *ret = kzalloc(size, flags);

	if (ret)
		ret->mad_size = mad_size;

	return ret;
}

static size_t port_mad_size(const struct ib_mad_port_private *port_priv)
{
	return rdma_max_mad_size(port_priv->device, port_priv->port_num);
}

static size_t mad_priv_dma_size(const struct ib_mad_private *mp)
{
	return sizeof(struct ib_grh) + mp->mad_size;
}

/*
 * Return 0 if SMP is to be sent
 * Return 1 if SMP was consumed locally (whether or not solicited)
 * Return < 0 if error
 */
static int handle_outgoing_dr_smp(struct ib_mad_agent_private *mad_agent_priv,
				  struct ib_mad_send_wr_private *mad_send_wr)
{
	int ret = 0;
	struct ib_smp *smp = mad_send_wr->send_buf.mad;
	struct opa_smp *opa_smp = (struct opa_smp *)smp;
	unsigned long flags;
	struct ib_mad_local_private *local;
	struct ib_mad_private *mad_priv;
	struct ib_mad_port_private *port_priv;
	struct ib_mad_agent_private *recv_mad_agent = NULL;
	struct ib_device *device = mad_agent_priv->agent.device;
	u8 port_num;
	struct ib_wc mad_wc;
	struct ib_ud_wr *send_wr = &mad_send_wr->send_wr;
	size_t mad_size = port_mad_size(mad_agent_priv->qp_info->port_priv);
	u16 out_mad_pkey_index = 0;
	u16 drslid;
	bool opa = rdma_cap_opa_mad(mad_agent_priv->qp_info->port_priv->device,
				    mad_agent_priv->qp_info->port_priv->port_num);

	if (rdma_cap_ib_switch(device) &&
	    smp->mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		port_num = send_wr->port_num;
	else
		port_num = mad_agent_priv->agent.port_num;

	/*
	 * Directed route handling starts if the initial LID routed part of
	 * a request or the ending LID routed part of a response is empty.
	 * If we are at the start of the LID routed part, don't update the
	 * hop_ptr or hop_cnt.  See section 14.2.2, Vol 1 IB spec.
	 */
	if (opa && smp->class_version == OPA_SM_CLASS_VERSION) {
		u32 opa_drslid;

		trace_ib_mad_handle_out_opa_smi(opa_smp);

		if ((opa_get_smp_direction(opa_smp)
		     ? opa_smp->route.dr.dr_dlid : opa_smp->route.dr.dr_slid) ==
		     OPA_LID_PERMISSIVE &&
		     opa_smi_handle_dr_smp_send(opa_smp,
						rdma_cap_ib_switch(device),
						port_num) == IB_SMI_DISCARD) {
			ret = -EINVAL;
			dev_err(&device->dev, "OPA Invalid directed route\n");
			goto out;
		}
		opa_drslid = be32_to_cpu(opa_smp->route.dr.dr_slid);
		if (opa_drslid != be32_to_cpu(OPA_LID_PERMISSIVE) &&
		    opa_drslid & 0xffff0000) {
			ret = -EINVAL;
			dev_err(&device->dev, "OPA Invalid dr_slid 0x%x\n",
			       opa_drslid);
			goto out;
		}
		drslid = (u16)(opa_drslid & 0x0000ffff);

		/* Check to post send on QP or process locally */
		if (opa_smi_check_local_smp(opa_smp, device) == IB_SMI_DISCARD &&
		    opa_smi_check_local_returning_smp(opa_smp, device) == IB_SMI_DISCARD)
			goto out;
	} else {
		trace_ib_mad_handle_out_ib_smi(smp);

		if ((ib_get_smp_direction(smp) ? smp->dr_dlid : smp->dr_slid) ==
		     IB_LID_PERMISSIVE &&
		     smi_handle_dr_smp_send(smp, rdma_cap_ib_switch(device), port_num) ==
		     IB_SMI_DISCARD) {
			ret = -EINVAL;
			dev_err(&device->dev, "Invalid directed route\n");
			goto out;
		}
		drslid = be16_to_cpu(smp->dr_slid);

		/* Check to post send on QP or process locally */
		if (smi_check_local_smp(smp, device) == IB_SMI_DISCARD &&
		    smi_check_local_returning_smp(smp, device) == IB_SMI_DISCARD)
			goto out;
	}

	local = kmalloc(sizeof *local, GFP_ATOMIC);
	if (!local) {
		ret = -ENOMEM;
		goto out;
	}
	local->mad_priv = NULL;
	local->recv_mad_agent = NULL;
	mad_priv = alloc_mad_private(mad_size, GFP_ATOMIC);
	if (!mad_priv) {
		ret = -ENOMEM;
		kfree(local);
		goto out;
	}

	build_smp_wc(mad_agent_priv->agent.qp,
		     send_wr->wr.wr_cqe, drslid,
		     send_wr->pkey_index,
		     send_wr->port_num, &mad_wc);

	if (opa && smp->base_version == OPA_MGMT_BASE_VERSION) {
		mad_wc.byte_len = mad_send_wr->send_buf.hdr_len
					+ mad_send_wr->send_buf.data_len
					+ sizeof(struct ib_grh);
	}

	/* No GRH for DR SMP */
	ret = device->ops.process_mad(device, 0, port_num, &mad_wc, NULL,
				      (const struct ib_mad *)smp,
				      (struct ib_mad *)mad_priv->mad, &mad_size,
				      &out_mad_pkey_index);
	switch (ret)
	{
	case IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY:
		if (ib_response_mad((const struct ib_mad_hdr *)mad_priv->mad) &&
		    mad_agent_priv->agent.recv_handler) {
			local->mad_priv = mad_priv;
			local->recv_mad_agent = mad_agent_priv;
			/*
			 * Reference MAD agent until receive
			 * side of local completion handled
			 */
			atomic_inc(&mad_agent_priv->refcount);
		} else
			kfree(mad_priv);
		break;
	case IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED:
		kfree(mad_priv);
		break;
	case IB_MAD_RESULT_SUCCESS:
		/* Treat like an incoming receive MAD */
		port_priv = ib_get_mad_port(mad_agent_priv->agent.device,
					    mad_agent_priv->agent.port_num);
		if (port_priv) {
			memcpy(mad_priv->mad, smp, mad_priv->mad_size);
			recv_mad_agent = find_mad_agent(port_priv,
						        (const struct ib_mad_hdr *)mad_priv->mad);
		}
		if (!port_priv || !recv_mad_agent) {
			/*
			 * No receiving agent so drop packet and
			 * generate send completion.
			 */
			kfree(mad_priv);
			break;
		}
		local->mad_priv = mad_priv;
		local->recv_mad_agent = recv_mad_agent;
		break;
	default:
		kfree(mad_priv);
		kfree(local);
		ret = -EINVAL;
		goto out;
	}

	local->mad_send_wr = mad_send_wr;
	if (opa) {
		local->mad_send_wr->send_wr.pkey_index = out_mad_pkey_index;
		local->return_wc_byte_len = mad_size;
	}
	/* Reference MAD agent until send side of local completion handled */
	atomic_inc(&mad_agent_priv->refcount);
	/* Queue local completion to local list */
	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	list_add_tail(&local->completion_list, &mad_agent_priv->local_list);
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
	queue_work(mad_agent_priv->qp_info->port_priv->wq,
		   &mad_agent_priv->local_work);
	ret = 1;
out:
	return ret;
}

static int get_pad_size(int hdr_len, int data_len, size_t mad_size)
{
	int seg_size, pad;

	seg_size = mad_size - hdr_len;
	if (data_len && seg_size) {
		pad = seg_size - data_len % seg_size;
		return pad == seg_size ? 0 : pad;
	} else
		return seg_size;
}

static void free_send_rmpp_list(struct ib_mad_send_wr_private *mad_send_wr)
{
	struct ib_rmpp_segment *s, *t;

	list_for_each_entry_safe(s, t, &mad_send_wr->rmpp_list, list) {
		list_del(&s->list);
		kfree(s);
	}
}

static int alloc_send_rmpp_list(struct ib_mad_send_wr_private *send_wr,
				size_t mad_size, gfp_t gfp_mask)
{
	struct ib_mad_send_buf *send_buf = &send_wr->send_buf;
	struct ib_rmpp_mad *rmpp_mad = send_buf->mad;
	struct ib_rmpp_segment *seg = NULL;
	int left, seg_size, pad;

	send_buf->seg_size = mad_size - send_buf->hdr_len;
	send_buf->seg_rmpp_size = mad_size - IB_MGMT_RMPP_HDR;
	seg_size = send_buf->seg_size;
	pad = send_wr->pad;

	/* Allocate data segments. */
	for (left = send_buf->data_len + pad; left > 0; left -= seg_size) {
		seg = kmalloc(sizeof (*seg) + seg_size, gfp_mask);
		if (!seg) {
			free_send_rmpp_list(send_wr);
			return -ENOMEM;
		}
		seg->num = ++send_buf->seg_count;
		list_add_tail(&seg->list, &send_wr->rmpp_list);
	}

	/* Zero any padding */
	if (pad)
		memset(seg->data + seg_size - pad, 0, pad);

	rmpp_mad->rmpp_hdr.rmpp_version = send_wr->mad_agent_priv->
					  agent.rmpp_version;
	rmpp_mad->rmpp_hdr.rmpp_type = IB_MGMT_RMPP_TYPE_DATA;
	ib_set_rmpp_flags(&rmpp_mad->rmpp_hdr, IB_MGMT_RMPP_FLAG_ACTIVE);

	send_wr->cur_seg = container_of(send_wr->rmpp_list.next,
					struct ib_rmpp_segment, list);
	send_wr->last_ack_seg = send_wr->cur_seg;
	return 0;
}

int ib_mad_kernel_rmpp_agent(const struct ib_mad_agent *agent)
{
	return agent->rmpp_version && !(agent->flags & IB_MAD_USER_RMPP);
}
EXPORT_SYMBOL(ib_mad_kernel_rmpp_agent);

struct ib_mad_send_buf * ib_create_send_mad(struct ib_mad_agent *mad_agent,
					    u32 remote_qpn, u16 pkey_index,
					    int rmpp_active,
					    int hdr_len, int data_len,
					    gfp_t gfp_mask,
					    u8 base_version)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_send_wr_private *mad_send_wr;
	int pad, message_size, ret, size;
	void *buf;
	size_t mad_size;
	bool opa;

	mad_agent_priv = container_of(mad_agent, struct ib_mad_agent_private,
				      agent);

	opa = rdma_cap_opa_mad(mad_agent->device, mad_agent->port_num);

	if (opa && base_version == OPA_MGMT_BASE_VERSION)
		mad_size = sizeof(struct opa_mad);
	else
		mad_size = sizeof(struct ib_mad);

	pad = get_pad_size(hdr_len, data_len, mad_size);
	message_size = hdr_len + data_len + pad;

	if (ib_mad_kernel_rmpp_agent(mad_agent)) {
		if (!rmpp_active && message_size > mad_size)
			return ERR_PTR(-EINVAL);
	} else
		if (rmpp_active || message_size > mad_size)
			return ERR_PTR(-EINVAL);

	size = rmpp_active ? hdr_len : mad_size;
	buf = kzalloc(sizeof *mad_send_wr + size, gfp_mask);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	mad_send_wr = buf + size;
	INIT_LIST_HEAD(&mad_send_wr->rmpp_list);
	mad_send_wr->send_buf.mad = buf;
	mad_send_wr->send_buf.hdr_len = hdr_len;
	mad_send_wr->send_buf.data_len = data_len;
	mad_send_wr->pad = pad;

	mad_send_wr->mad_agent_priv = mad_agent_priv;
	mad_send_wr->sg_list[0].length = hdr_len;
	mad_send_wr->sg_list[0].lkey = mad_agent->qp->pd->local_dma_lkey;

	/* OPA MADs don't have to be the full 2048 bytes */
	if (opa && base_version == OPA_MGMT_BASE_VERSION &&
	    data_len < mad_size - hdr_len)
		mad_send_wr->sg_list[1].length = data_len;
	else
		mad_send_wr->sg_list[1].length = mad_size - hdr_len;

	mad_send_wr->sg_list[1].lkey = mad_agent->qp->pd->local_dma_lkey;

	mad_send_wr->mad_list.cqe.done = ib_mad_send_done;

	mad_send_wr->send_wr.wr.wr_cqe = &mad_send_wr->mad_list.cqe;
	mad_send_wr->send_wr.wr.sg_list = mad_send_wr->sg_list;
	mad_send_wr->send_wr.wr.num_sge = 2;
	mad_send_wr->send_wr.wr.opcode = IB_WR_SEND;
	mad_send_wr->send_wr.wr.send_flags = IB_SEND_SIGNALED;
	mad_send_wr->send_wr.remote_qpn = remote_qpn;
	mad_send_wr->send_wr.remote_qkey = IB_QP_SET_QKEY;
	mad_send_wr->send_wr.pkey_index = pkey_index;

	if (rmpp_active) {
		ret = alloc_send_rmpp_list(mad_send_wr, mad_size, gfp_mask);
		if (ret) {
			kfree(buf);
			return ERR_PTR(ret);
		}
	}

	mad_send_wr->send_buf.mad_agent = mad_agent;
	atomic_inc(&mad_agent_priv->refcount);
	return &mad_send_wr->send_buf;
}
EXPORT_SYMBOL(ib_create_send_mad);

int ib_get_mad_data_offset(u8 mgmt_class)
{
	if (mgmt_class == IB_MGMT_CLASS_SUBN_ADM)
		return IB_MGMT_SA_HDR;
	else if ((mgmt_class == IB_MGMT_CLASS_DEVICE_MGMT) ||
		 (mgmt_class == IB_MGMT_CLASS_DEVICE_ADM) ||
		 (mgmt_class == IB_MGMT_CLASS_BIS))
		return IB_MGMT_DEVICE_HDR;
	else if ((mgmt_class >= IB_MGMT_CLASS_VENDOR_RANGE2_START) &&
		 (mgmt_class <= IB_MGMT_CLASS_VENDOR_RANGE2_END))
		return IB_MGMT_VENDOR_HDR;
	else
		return IB_MGMT_MAD_HDR;
}
EXPORT_SYMBOL(ib_get_mad_data_offset);

int ib_is_mad_class_rmpp(u8 mgmt_class)
{
	if ((mgmt_class == IB_MGMT_CLASS_SUBN_ADM) ||
	    (mgmt_class == IB_MGMT_CLASS_DEVICE_MGMT) ||
	    (mgmt_class == IB_MGMT_CLASS_DEVICE_ADM) ||
	    (mgmt_class == IB_MGMT_CLASS_BIS) ||
	    ((mgmt_class >= IB_MGMT_CLASS_VENDOR_RANGE2_START) &&
	     (mgmt_class <= IB_MGMT_CLASS_VENDOR_RANGE2_END)))
		return 1;
	return 0;
}
EXPORT_SYMBOL(ib_is_mad_class_rmpp);

void *ib_get_rmpp_segment(struct ib_mad_send_buf *send_buf, int seg_num)
{
	struct ib_mad_send_wr_private *mad_send_wr;
	struct list_head *list;

	mad_send_wr = container_of(send_buf, struct ib_mad_send_wr_private,
				   send_buf);
	list = &mad_send_wr->cur_seg->list;

	if (mad_send_wr->cur_seg->num < seg_num) {
		list_for_each_entry(mad_send_wr->cur_seg, list, list)
			if (mad_send_wr->cur_seg->num == seg_num)
				break;
	} else if (mad_send_wr->cur_seg->num > seg_num) {
		list_for_each_entry_reverse(mad_send_wr->cur_seg, list, list)
			if (mad_send_wr->cur_seg->num == seg_num)
				break;
	}
	return mad_send_wr->cur_seg->data;
}
EXPORT_SYMBOL(ib_get_rmpp_segment);

static inline void *ib_get_payload(struct ib_mad_send_wr_private *mad_send_wr)
{
	if (mad_send_wr->send_buf.seg_count)
		return ib_get_rmpp_segment(&mad_send_wr->send_buf,
					   mad_send_wr->seg_num);
	else
		return mad_send_wr->send_buf.mad +
		       mad_send_wr->send_buf.hdr_len;
}

void ib_free_send_mad(struct ib_mad_send_buf *send_buf)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_send_wr_private *mad_send_wr;

	mad_agent_priv = container_of(send_buf->mad_agent,
				      struct ib_mad_agent_private, agent);
	mad_send_wr = container_of(send_buf, struct ib_mad_send_wr_private,
				   send_buf);

	free_send_rmpp_list(mad_send_wr);
	kfree(send_buf->mad);
	deref_mad_agent(mad_agent_priv);
}
EXPORT_SYMBOL(ib_free_send_mad);

int ib_send_mad(struct ib_mad_send_wr_private *mad_send_wr)
{
	struct ib_mad_qp_info *qp_info;
	struct list_head *list;
	struct ib_mad_agent *mad_agent;
	struct ib_sge *sge;
	unsigned long flags;
	int ret;

	/* Set WR ID to find mad_send_wr upon completion */
	qp_info = mad_send_wr->mad_agent_priv->qp_info;
	mad_send_wr->mad_list.mad_queue = &qp_info->send_queue;
	mad_send_wr->mad_list.cqe.done = ib_mad_send_done;
	mad_send_wr->send_wr.wr.wr_cqe = &mad_send_wr->mad_list.cqe;

	mad_agent = mad_send_wr->send_buf.mad_agent;
	sge = mad_send_wr->sg_list;
	sge[0].addr = ib_dma_map_single(mad_agent->device,
					mad_send_wr->send_buf.mad,
					sge[0].length,
					DMA_TO_DEVICE);
	if (unlikely(ib_dma_mapping_error(mad_agent->device, sge[0].addr)))
		return -ENOMEM;

	mad_send_wr->header_mapping = sge[0].addr;

	sge[1].addr = ib_dma_map_single(mad_agent->device,
					ib_get_payload(mad_send_wr),
					sge[1].length,
					DMA_TO_DEVICE);
	if (unlikely(ib_dma_mapping_error(mad_agent->device, sge[1].addr))) {
		ib_dma_unmap_single(mad_agent->device,
				    mad_send_wr->header_mapping,
				    sge[0].length, DMA_TO_DEVICE);
		return -ENOMEM;
	}
	mad_send_wr->payload_mapping = sge[1].addr;

	spin_lock_irqsave(&qp_info->send_queue.lock, flags);
	if (qp_info->send_queue.count < qp_info->send_queue.max_active) {
		trace_ib_mad_ib_send_mad(mad_send_wr, qp_info);
		ret = ib_post_send(mad_agent->qp, &mad_send_wr->send_wr.wr,
				   NULL);
		list = &qp_info->send_queue.list;
	} else {
		ret = 0;
		list = &qp_info->overflow_list;
	}

	if (!ret) {
		qp_info->send_queue.count++;
		list_add_tail(&mad_send_wr->mad_list.list, list);
	}
	spin_unlock_irqrestore(&qp_info->send_queue.lock, flags);
	if (ret) {
		ib_dma_unmap_single(mad_agent->device,
				    mad_send_wr->header_mapping,
				    sge[0].length, DMA_TO_DEVICE);
		ib_dma_unmap_single(mad_agent->device,
				    mad_send_wr->payload_mapping,
				    sge[1].length, DMA_TO_DEVICE);
	}
	return ret;
}

/*
 * ib_post_send_mad - Posts MAD(s) to the send queue of the QP associated
 *  with the registered client
 */
int ib_post_send_mad(struct ib_mad_send_buf *send_buf,
		     struct ib_mad_send_buf **bad_send_buf)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_send_buf *next_send_buf;
	struct ib_mad_send_wr_private *mad_send_wr;
	unsigned long flags;
	int ret = -EINVAL;

	/* Walk list of send WRs and post each on send list */
	for (; send_buf; send_buf = next_send_buf) {
		mad_send_wr = container_of(send_buf,
					   struct ib_mad_send_wr_private,
					   send_buf);
		mad_agent_priv = mad_send_wr->mad_agent_priv;

		ret = ib_mad_enforce_security(mad_agent_priv,
					      mad_send_wr->send_wr.pkey_index);
		if (ret)
			goto error;

		if (!send_buf->mad_agent->send_handler ||
		    (send_buf->timeout_ms &&
		     !send_buf->mad_agent->recv_handler)) {
			ret = -EINVAL;
			goto error;
		}

		if (!ib_is_mad_class_rmpp(((struct ib_mad_hdr *) send_buf->mad)->mgmt_class)) {
			if (mad_agent_priv->agent.rmpp_version) {
				ret = -EINVAL;
				goto error;
			}
		}

		/*
		 * Save pointer to next work request to post in case the
		 * current one completes, and the user modifies the work
		 * request associated with the completion
		 */
		next_send_buf = send_buf->next;
		mad_send_wr->send_wr.ah = send_buf->ah;

		if (((struct ib_mad_hdr *) send_buf->mad)->mgmt_class ==
		    IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) {
			ret = handle_outgoing_dr_smp(mad_agent_priv,
						     mad_send_wr);
			if (ret < 0)		/* error */
				goto error;
			else if (ret == 1)	/* locally consumed */
				continue;
		}

		mad_send_wr->tid = ((struct ib_mad_hdr *) send_buf->mad)->tid;
		/* Timeout will be updated after send completes */
		mad_send_wr->timeout = msecs_to_jiffies(send_buf->timeout_ms);
		mad_send_wr->max_retries = send_buf->retries;
		mad_send_wr->retries_left = send_buf->retries;
		send_buf->retries = 0;
		/* Reference for work request to QP + response */
		mad_send_wr->refcount = 1 + (mad_send_wr->timeout > 0);
		mad_send_wr->status = IB_WC_SUCCESS;

		/* Reference MAD agent until send completes */
		atomic_inc(&mad_agent_priv->refcount);
		spin_lock_irqsave(&mad_agent_priv->lock, flags);
		list_add_tail(&mad_send_wr->agent_list,
			      &mad_agent_priv->send_list);
		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

		if (ib_mad_kernel_rmpp_agent(&mad_agent_priv->agent)) {
			ret = ib_send_rmpp_mad(mad_send_wr);
			if (ret >= 0 && ret != IB_RMPP_RESULT_CONSUMED)
				ret = ib_send_mad(mad_send_wr);
		} else
			ret = ib_send_mad(mad_send_wr);
		if (ret < 0) {
			/* Fail send request */
			spin_lock_irqsave(&mad_agent_priv->lock, flags);
			list_del(&mad_send_wr->agent_list);
			spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
			atomic_dec(&mad_agent_priv->refcount);
			goto error;
		}
	}
	return 0;
error:
	if (bad_send_buf)
		*bad_send_buf = send_buf;
	return ret;
}
EXPORT_SYMBOL(ib_post_send_mad);

/*
 * ib_free_recv_mad - Returns data buffers used to receive
 *  a MAD to the access layer
 */
void ib_free_recv_mad(struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_mad_recv_buf *mad_recv_buf, *temp_recv_buf;
	struct ib_mad_private_header *mad_priv_hdr;
	struct ib_mad_private *priv;
	struct list_head free_list;

	INIT_LIST_HEAD(&free_list);
	list_splice_init(&mad_recv_wc->rmpp_list, &free_list);

	list_for_each_entry_safe(mad_recv_buf, temp_recv_buf,
					&free_list, list) {
		mad_recv_wc = container_of(mad_recv_buf, struct ib_mad_recv_wc,
					   recv_buf);
		mad_priv_hdr = container_of(mad_recv_wc,
					    struct ib_mad_private_header,
					    recv_wc);
		priv = container_of(mad_priv_hdr, struct ib_mad_private,
				    header);
		kfree(priv);
	}
}
EXPORT_SYMBOL(ib_free_recv_mad);

static int method_in_use(struct ib_mad_mgmt_method_table **method,
			 struct ib_mad_reg_req *mad_reg_req)
{
	int i;

	for_each_set_bit(i, mad_reg_req->method_mask, IB_MGMT_MAX_METHODS) {
		if ((*method)->agent[i]) {
			pr_err("Method %d already in use\n", i);
			return -EINVAL;
		}
	}
	return 0;
}

static int allocate_method_table(struct ib_mad_mgmt_method_table **method)
{
	/* Allocate management method table */
	*method = kzalloc(sizeof **method, GFP_ATOMIC);
	return (*method) ? 0 : (-ENOMEM);
}

/*
 * Check to see if there are any methods still in use
 */
static int check_method_table(struct ib_mad_mgmt_method_table *method)
{
	int i;

	for (i = 0; i < IB_MGMT_MAX_METHODS; i++)
		if (method->agent[i])
			return 1;
	return 0;
}

/*
 * Check to see if there are any method tables for this class still in use
 */
static int check_class_table(struct ib_mad_mgmt_class_table *class)
{
	int i;

	for (i = 0; i < MAX_MGMT_CLASS; i++)
		if (class->method_table[i])
			return 1;
	return 0;
}

static int check_vendor_class(struct ib_mad_mgmt_vendor_class *vendor_class)
{
	int i;

	for (i = 0; i < MAX_MGMT_OUI; i++)
		if (vendor_class->method_table[i])
			return 1;
	return 0;
}

static int find_vendor_oui(struct ib_mad_mgmt_vendor_class *vendor_class,
			   const char *oui)
{
	int i;

	for (i = 0; i < MAX_MGMT_OUI; i++)
		/* Is there matching OUI for this vendor class ? */
		if (!memcmp(vendor_class->oui[i], oui, 3))
			return i;

	return -1;
}

static int check_vendor_table(struct ib_mad_mgmt_vendor_class_table *vendor)
{
	int i;

	for (i = 0; i < MAX_MGMT_VENDOR_RANGE2; i++)
		if (vendor->vendor_class[i])
			return 1;

	return 0;
}

static void remove_methods_mad_agent(struct ib_mad_mgmt_method_table *method,
				     struct ib_mad_agent_private *agent)
{
	int i;

	/* Remove any methods for this mad agent */
	for (i = 0; i < IB_MGMT_MAX_METHODS; i++) {
		if (method->agent[i] == agent) {
			method->agent[i] = NULL;
		}
	}
}

static int add_nonoui_reg_req(struct ib_mad_reg_req *mad_reg_req,
			      struct ib_mad_agent_private *agent_priv,
			      u8 mgmt_class)
{
	struct ib_mad_port_private *port_priv;
	struct ib_mad_mgmt_class_table **class;
	struct ib_mad_mgmt_method_table **method;
	int i, ret;

	port_priv = agent_priv->qp_info->port_priv;
	class = &port_priv->version[mad_reg_req->mgmt_class_version].class;
	if (!*class) {
		/* Allocate management class table for "new" class version */
		*class = kzalloc(sizeof **class, GFP_ATOMIC);
		if (!*class) {
			ret = -ENOMEM;
			goto error1;
		}

		/* Allocate method table for this management class */
		method = &(*class)->method_table[mgmt_class];
		if ((ret = allocate_method_table(method)))
			goto error2;
	} else {
		method = &(*class)->method_table[mgmt_class];
		if (!*method) {
			/* Allocate method table for this management class */
			if ((ret = allocate_method_table(method)))
				goto error1;
		}
	}

	/* Now, make sure methods are not already in use */
	if (method_in_use(method, mad_reg_req))
		goto error3;

	/* Finally, add in methods being registered */
	for_each_set_bit(i, mad_reg_req->method_mask, IB_MGMT_MAX_METHODS)
		(*method)->agent[i] = agent_priv;

	return 0;

error3:
	/* Remove any methods for this mad agent */
	remove_methods_mad_agent(*method, agent_priv);
	/* Now, check to see if there are any methods in use */
	if (!check_method_table(*method)) {
		/* If not, release management method table */
		kfree(*method);
		*method = NULL;
	}
	ret = -EINVAL;
	goto error1;
error2:
	kfree(*class);
	*class = NULL;
error1:
	return ret;
}

static int add_oui_reg_req(struct ib_mad_reg_req *mad_reg_req,
			   struct ib_mad_agent_private *agent_priv)
{
	struct ib_mad_port_private *port_priv;
	struct ib_mad_mgmt_vendor_class_table **vendor_table;
	struct ib_mad_mgmt_vendor_class_table *vendor = NULL;
	struct ib_mad_mgmt_vendor_class *vendor_class = NULL;
	struct ib_mad_mgmt_method_table **method;
	int i, ret = -ENOMEM;
	u8 vclass;

	/* "New" vendor (with OUI) class */
	vclass = vendor_class_index(mad_reg_req->mgmt_class);
	port_priv = agent_priv->qp_info->port_priv;
	vendor_table = &port_priv->version[
				mad_reg_req->mgmt_class_version].vendor;
	if (!*vendor_table) {
		/* Allocate mgmt vendor class table for "new" class version */
		vendor = kzalloc(sizeof *vendor, GFP_ATOMIC);
		if (!vendor)
			goto error1;

		*vendor_table = vendor;
	}
	if (!(*vendor_table)->vendor_class[vclass]) {
		/* Allocate table for this management vendor class */
		vendor_class = kzalloc(sizeof *vendor_class, GFP_ATOMIC);
		if (!vendor_class)
			goto error2;

		(*vendor_table)->vendor_class[vclass] = vendor_class;
	}
	for (i = 0; i < MAX_MGMT_OUI; i++) {
		/* Is there matching OUI for this vendor class ? */
		if (!memcmp((*vendor_table)->vendor_class[vclass]->oui[i],
			    mad_reg_req->oui, 3)) {
			method = &(*vendor_table)->vendor_class[
						vclass]->method_table[i];
			if (!*method)
				goto error3;
			goto check_in_use;
		}
	}
	for (i = 0; i < MAX_MGMT_OUI; i++) {
		/* OUI slot available ? */
		if (!is_vendor_oui((*vendor_table)->vendor_class[
				vclass]->oui[i])) {
			method = &(*vendor_table)->vendor_class[
				vclass]->method_table[i];
			/* Allocate method table for this OUI */
			if (!*method) {
				ret = allocate_method_table(method);
				if (ret)
					goto error3;
			}
			memcpy((*vendor_table)->vendor_class[vclass]->oui[i],
			       mad_reg_req->oui, 3);
			goto check_in_use;
		}
	}
	dev_err(&agent_priv->agent.device->dev, "All OUI slots in use\n");
	goto error3;

check_in_use:
	/* Now, make sure methods are not already in use */
	if (method_in_use(method, mad_reg_req))
		goto error4;

	/* Finally, add in methods being registered */
	for_each_set_bit(i, mad_reg_req->method_mask, IB_MGMT_MAX_METHODS)
		(*method)->agent[i] = agent_priv;

	return 0;

error4:
	/* Remove any methods for this mad agent */
	remove_methods_mad_agent(*method, agent_priv);
	/* Now, check to see if there are any methods in use */
	if (!check_method_table(*method)) {
		/* If not, release management method table */
		kfree(*method);
		*method = NULL;
	}
	ret = -EINVAL;
error3:
	if (vendor_class) {
		(*vendor_table)->vendor_class[vclass] = NULL;
		kfree(vendor_class);
	}
error2:
	if (vendor) {
		*vendor_table = NULL;
		kfree(vendor);
	}
error1:
	return ret;
}

static void remove_mad_reg_req(struct ib_mad_agent_private *agent_priv)
{
	struct ib_mad_port_private *port_priv;
	struct ib_mad_mgmt_class_table *class;
	struct ib_mad_mgmt_method_table *method;
	struct ib_mad_mgmt_vendor_class_table *vendor;
	struct ib_mad_mgmt_vendor_class *vendor_class;
	int index;
	u8 mgmt_class;

	/*
	 * Was MAD registration request supplied
	 * with original registration ?
	 */
	if (!agent_priv->reg_req) {
		goto out;
	}

	port_priv = agent_priv->qp_info->port_priv;
	mgmt_class = convert_mgmt_class(agent_priv->reg_req->mgmt_class);
	class = port_priv->version[
			agent_priv->reg_req->mgmt_class_version].class;
	if (!class)
		goto vendor_check;

	method = class->method_table[mgmt_class];
	if (method) {
		/* Remove any methods for this mad agent */
		remove_methods_mad_agent(method, agent_priv);
		/* Now, check to see if there are any methods still in use */
		if (!check_method_table(method)) {
			/* If not, release management method table */
			kfree(method);
			class->method_table[mgmt_class] = NULL;
			/* Any management classes left ? */
			if (!check_class_table(class)) {
				/* If not, release management class table */
				kfree(class);
				port_priv->version[
					agent_priv->reg_req->
					mgmt_class_version].class = NULL;
			}
		}
	}

vendor_check:
	if (!is_vendor_class(mgmt_class))
		goto out;

	/* normalize mgmt_class to vendor range 2 */
	mgmt_class = vendor_class_index(agent_priv->reg_req->mgmt_class);
	vendor = port_priv->version[
			agent_priv->reg_req->mgmt_class_version].vendor;

	if (!vendor)
		goto out;

	vendor_class = vendor->vendor_class[mgmt_class];
	if (vendor_class) {
		index = find_vendor_oui(vendor_class, agent_priv->reg_req->oui);
		if (index < 0)
			goto out;
		method = vendor_class->method_table[index];
		if (method) {
			/* Remove any methods for this mad agent */
			remove_methods_mad_agent(method, agent_priv);
			/*
			 * Now, check to see if there are
			 * any methods still in use
			 */
			if (!check_method_table(method)) {
				/* If not, release management method table */
				kfree(method);
				vendor_class->method_table[index] = NULL;
				memset(vendor_class->oui[index], 0, 3);
				/* Any OUIs left ? */
				if (!check_vendor_class(vendor_class)) {
					/* If not, release vendor class table */
					kfree(vendor_class);
					vendor->vendor_class[mgmt_class] = NULL;
					/* Any other vendor classes left ? */
					if (!check_vendor_table(vendor)) {
						kfree(vendor);
						port_priv->version[
							agent_priv->reg_req->
							mgmt_class_version].
							vendor = NULL;
					}
				}
			}
		}
	}

out:
	return;
}

static struct ib_mad_agent_private *
find_mad_agent(struct ib_mad_port_private *port_priv,
	       const struct ib_mad_hdr *mad_hdr)
{
	struct ib_mad_agent_private *mad_agent = NULL;
	unsigned long flags;

	if (ib_response_mad(mad_hdr)) {
		u32 hi_tid;

		/*
		 * Routing is based on high 32 bits of transaction ID
		 * of MAD.
		 */
		hi_tid = be64_to_cpu(mad_hdr->tid) >> 32;
		rcu_read_lock();
		mad_agent = xa_load(&ib_mad_clients, hi_tid);
		if (mad_agent && !atomic_inc_not_zero(&mad_agent->refcount))
			mad_agent = NULL;
		rcu_read_unlock();
	} else {
		struct ib_mad_mgmt_class_table *class;
		struct ib_mad_mgmt_method_table *method;
		struct ib_mad_mgmt_vendor_class_table *vendor;
		struct ib_mad_mgmt_vendor_class *vendor_class;
		const struct ib_vendor_mad *vendor_mad;
		int index;

		spin_lock_irqsave(&port_priv->reg_lock, flags);
		/*
		 * Routing is based on version, class, and method
		 * For "newer" vendor MADs, also based on OUI
		 */
		if (mad_hdr->class_version >= MAX_MGMT_VERSION)
			goto out;
		if (!is_vendor_class(mad_hdr->mgmt_class)) {
			class = port_priv->version[
					mad_hdr->class_version].class;
			if (!class)
				goto out;
			if (convert_mgmt_class(mad_hdr->mgmt_class) >=
			    ARRAY_SIZE(class->method_table))
				goto out;
			method = class->method_table[convert_mgmt_class(
							mad_hdr->mgmt_class)];
			if (method)
				mad_agent = method->agent[mad_hdr->method &
							  ~IB_MGMT_METHOD_RESP];
		} else {
			vendor = port_priv->version[
					mad_hdr->class_version].vendor;
			if (!vendor)
				goto out;
			vendor_class = vendor->vendor_class[vendor_class_index(
						mad_hdr->mgmt_class)];
			if (!vendor_class)
				goto out;
			/* Find matching OUI */
			vendor_mad = (const struct ib_vendor_mad *)mad_hdr;
			index = find_vendor_oui(vendor_class, vendor_mad->oui);
			if (index == -1)
				goto out;
			method = vendor_class->method_table[index];
			if (method) {
				mad_agent = method->agent[mad_hdr->method &
							  ~IB_MGMT_METHOD_RESP];
			}
		}
		if (mad_agent)
			atomic_inc(&mad_agent->refcount);
out:
		spin_unlock_irqrestore(&port_priv->reg_lock, flags);
	}

	if (mad_agent && !mad_agent->agent.recv_handler) {
		dev_notice(&port_priv->device->dev,
			   "No receive handler for client %p on port %d\n",
			   &mad_agent->agent, port_priv->port_num);
		deref_mad_agent(mad_agent);
		mad_agent = NULL;
	}

	return mad_agent;
}

static int validate_mad(const struct ib_mad_hdr *mad_hdr,
			const struct ib_mad_qp_info *qp_info,
			bool opa)
{
	int valid = 0;
	u32 qp_num = qp_info->qp->qp_num;

	/* Make sure MAD base version is understood */
	if (mad_hdr->base_version != IB_MGMT_BASE_VERSION &&
	    (!opa || mad_hdr->base_version != OPA_MGMT_BASE_VERSION)) {
		pr_err("MAD received with unsupported base version %d %s\n",
		       mad_hdr->base_version, opa ? "(opa)" : "");
		goto out;
	}

	/* Filter SMI packets sent to other than QP0 */
	if ((mad_hdr->mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED) ||
	    (mad_hdr->mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)) {
		if (qp_num == 0)
			valid = 1;
	} else {
		/* CM attributes other than ClassPortInfo only use Send method */
		if ((mad_hdr->mgmt_class == IB_MGMT_CLASS_CM) &&
		    (mad_hdr->attr_id != IB_MGMT_CLASSPORTINFO_ATTR_ID) &&
		    (mad_hdr->method != IB_MGMT_METHOD_SEND))
			goto out;
		/* Filter GSI packets sent to QP0 */
		if (qp_num != 0)
			valid = 1;
	}

out:
	return valid;
}

static int is_rmpp_data_mad(const struct ib_mad_agent_private *mad_agent_priv,
			    const struct ib_mad_hdr *mad_hdr)
{
	struct ib_rmpp_mad *rmpp_mad;

	rmpp_mad = (struct ib_rmpp_mad *)mad_hdr;
	return !mad_agent_priv->agent.rmpp_version ||
		!ib_mad_kernel_rmpp_agent(&mad_agent_priv->agent) ||
		!(ib_get_rmpp_flags(&rmpp_mad->rmpp_hdr) &
				    IB_MGMT_RMPP_FLAG_ACTIVE) ||
		(rmpp_mad->rmpp_hdr.rmpp_type == IB_MGMT_RMPP_TYPE_DATA);
}

static inline int rcv_has_same_class(const struct ib_mad_send_wr_private *wr,
				     const struct ib_mad_recv_wc *rwc)
{
	return ((struct ib_mad_hdr *)(wr->send_buf.mad))->mgmt_class ==
		rwc->recv_buf.mad->mad_hdr.mgmt_class;
}

static inline int rcv_has_same_gid(const struct ib_mad_agent_private *mad_agent_priv,
				   const struct ib_mad_send_wr_private *wr,
				   const struct ib_mad_recv_wc *rwc )
{
	struct rdma_ah_attr attr;
	u8 send_resp, rcv_resp;
	union ib_gid sgid;
	struct ib_device *device = mad_agent_priv->agent.device;
	u8 port_num = mad_agent_priv->agent.port_num;
	u8 lmc;
	bool has_grh;

	send_resp = ib_response_mad((struct ib_mad_hdr *)wr->send_buf.mad);
	rcv_resp = ib_response_mad(&rwc->recv_buf.mad->mad_hdr);

	if (send_resp == rcv_resp)
		/* both requests, or both responses. GIDs different */
		return 0;

	if (rdma_query_ah(wr->send_buf.ah, &attr))
		/* Assume not equal, to avoid false positives. */
		return 0;

	has_grh = !!(rdma_ah_get_ah_flags(&attr) & IB_AH_GRH);
	if (has_grh != !!(rwc->wc->wc_flags & IB_WC_GRH))
		/* one has GID, other does not.  Assume different */
		return 0;

	if (!send_resp && rcv_resp) {
		/* is request/response. */
		if (!has_grh) {
			if (ib_get_cached_lmc(device, port_num, &lmc))
				return 0;
			return (!lmc || !((rdma_ah_get_path_bits(&attr) ^
					   rwc->wc->dlid_path_bits) &
					  ((1 << lmc) - 1)));
		} else {
			const struct ib_global_route *grh =
					rdma_ah_read_grh(&attr);

			if (rdma_query_gid(device, port_num,
					   grh->sgid_index, &sgid))
				return 0;
			return !memcmp(sgid.raw, rwc->recv_buf.grh->dgid.raw,
				       16);
		}
	}

	if (!has_grh)
		return rdma_ah_get_dlid(&attr) == rwc->wc->slid;
	else
		return !memcmp(rdma_ah_read_grh(&attr)->dgid.raw,
			       rwc->recv_buf.grh->sgid.raw,
			       16);
}

static inline int is_direct(u8 class)
{
	return (class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE);
}

struct ib_mad_send_wr_private*
ib_find_send_mad(const struct ib_mad_agent_private *mad_agent_priv,
		 const struct ib_mad_recv_wc *wc)
{
	struct ib_mad_send_wr_private *wr;
	const struct ib_mad_hdr *mad_hdr;

	mad_hdr = &wc->recv_buf.mad->mad_hdr;

	list_for_each_entry(wr, &mad_agent_priv->wait_list, agent_list) {
		if ((wr->tid == mad_hdr->tid) &&
		    rcv_has_same_class(wr, wc) &&
		    /*
		     * Don't check GID for direct routed MADs.
		     * These might have permissive LIDs.
		     */
		    (is_direct(mad_hdr->mgmt_class) ||
		     rcv_has_same_gid(mad_agent_priv, wr, wc)))
			return (wr->status == IB_WC_SUCCESS) ? wr : NULL;
	}

	/*
	 * It's possible to receive the response before we've
	 * been notified that the send has completed
	 */
	list_for_each_entry(wr, &mad_agent_priv->send_list, agent_list) {
		if (is_rmpp_data_mad(mad_agent_priv, wr->send_buf.mad) &&
		    wr->tid == mad_hdr->tid &&
		    wr->timeout &&
		    rcv_has_same_class(wr, wc) &&
		    /*
		     * Don't check GID for direct routed MADs.
		     * These might have permissive LIDs.
		     */
		    (is_direct(mad_hdr->mgmt_class) ||
		     rcv_has_same_gid(mad_agent_priv, wr, wc)))
			/* Verify request has not been canceled */
			return (wr->status == IB_WC_SUCCESS) ? wr : NULL;
	}
	return NULL;
}

void ib_mark_mad_done(struct ib_mad_send_wr_private *mad_send_wr)
{
	mad_send_wr->timeout = 0;
	if (mad_send_wr->refcount == 1)
		list_move_tail(&mad_send_wr->agent_list,
			      &mad_send_wr->mad_agent_priv->done_list);
}

static void ib_mad_complete_recv(struct ib_mad_agent_private *mad_agent_priv,
				 struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_mad_send_wr_private *mad_send_wr;
	struct ib_mad_send_wc mad_send_wc;
	unsigned long flags;
	int ret;

	INIT_LIST_HEAD(&mad_recv_wc->rmpp_list);
	ret = ib_mad_enforce_security(mad_agent_priv,
				      mad_recv_wc->wc->pkey_index);
	if (ret) {
		ib_free_recv_mad(mad_recv_wc);
		deref_mad_agent(mad_agent_priv);
		return;
	}

	list_add(&mad_recv_wc->recv_buf.list, &mad_recv_wc->rmpp_list);
	if (ib_mad_kernel_rmpp_agent(&mad_agent_priv->agent)) {
		mad_recv_wc = ib_process_rmpp_recv_wc(mad_agent_priv,
						      mad_recv_wc);
		if (!mad_recv_wc) {
			deref_mad_agent(mad_agent_priv);
			return;
		}
	}

	/* Complete corresponding request */
	if (ib_response_mad(&mad_recv_wc->recv_buf.mad->mad_hdr)) {
		spin_lock_irqsave(&mad_agent_priv->lock, flags);
		mad_send_wr = ib_find_send_mad(mad_agent_priv, mad_recv_wc);
		if (!mad_send_wr) {
			spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
			if (!ib_mad_kernel_rmpp_agent(&mad_agent_priv->agent)
			   && ib_is_mad_class_rmpp(mad_recv_wc->recv_buf.mad->mad_hdr.mgmt_class)
			   && (ib_get_rmpp_flags(&((struct ib_rmpp_mad *)mad_recv_wc->recv_buf.mad)->rmpp_hdr)
					& IB_MGMT_RMPP_FLAG_ACTIVE)) {
				/* user rmpp is in effect
				 * and this is an active RMPP MAD
				 */
				mad_agent_priv->agent.recv_handler(
						&mad_agent_priv->agent, NULL,
						mad_recv_wc);
				atomic_dec(&mad_agent_priv->refcount);
			} else {
				/* not user rmpp, revert to normal behavior and
				 * drop the mad */
				ib_free_recv_mad(mad_recv_wc);
				deref_mad_agent(mad_agent_priv);
				return;
			}
		} else {
			ib_mark_mad_done(mad_send_wr);
			spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

			/* Defined behavior is to complete response before request */
			mad_agent_priv->agent.recv_handler(
					&mad_agent_priv->agent,
					&mad_send_wr->send_buf,
					mad_recv_wc);
			atomic_dec(&mad_agent_priv->refcount);

			mad_send_wc.status = IB_WC_SUCCESS;
			mad_send_wc.vendor_err = 0;
			mad_send_wc.send_buf = &mad_send_wr->send_buf;
			ib_mad_complete_send_wr(mad_send_wr, &mad_send_wc);
		}
	} else {
		mad_agent_priv->agent.recv_handler(&mad_agent_priv->agent, NULL,
						   mad_recv_wc);
		deref_mad_agent(mad_agent_priv);
	}

	return;
}

static enum smi_action handle_ib_smi(const struct ib_mad_port_private *port_priv,
				     const struct ib_mad_qp_info *qp_info,
				     const struct ib_wc *wc,
				     int port_num,
				     struct ib_mad_private *recv,
				     struct ib_mad_private *response)
{
	enum smi_forward_action retsmi;
	struct ib_smp *smp = (struct ib_smp *)recv->mad;

	trace_ib_mad_handle_ib_smi(smp);

	if (smi_handle_dr_smp_recv(smp,
				   rdma_cap_ib_switch(port_priv->device),
				   port_num,
				   port_priv->device->phys_port_cnt) ==
				   IB_SMI_DISCARD)
		return IB_SMI_DISCARD;

	retsmi = smi_check_forward_dr_smp(smp);
	if (retsmi == IB_SMI_LOCAL)
		return IB_SMI_HANDLE;

	if (retsmi == IB_SMI_SEND) { /* don't forward */
		if (smi_handle_dr_smp_send(smp,
					   rdma_cap_ib_switch(port_priv->device),
					   port_num) == IB_SMI_DISCARD)
			return IB_SMI_DISCARD;

		if (smi_check_local_smp(smp, port_priv->device) == IB_SMI_DISCARD)
			return IB_SMI_DISCARD;
	} else if (rdma_cap_ib_switch(port_priv->device)) {
		/* forward case for switches */
		memcpy(response, recv, mad_priv_size(response));
		response->header.recv_wc.wc = &response->header.wc;
		response->header.recv_wc.recv_buf.mad = (struct ib_mad *)response->mad;
		response->header.recv_wc.recv_buf.grh = &response->grh;

		agent_send_response((const struct ib_mad_hdr *)response->mad,
				    &response->grh, wc,
				    port_priv->device,
				    smi_get_fwd_port(smp),
				    qp_info->qp->qp_num,
				    response->mad_size,
				    false);

		return IB_SMI_DISCARD;
	}
	return IB_SMI_HANDLE;
}

static bool generate_unmatched_resp(const struct ib_mad_private *recv,
				    struct ib_mad_private *response,
				    size_t *resp_len, bool opa)
{
	const struct ib_mad_hdr *recv_hdr = (const struct ib_mad_hdr *)recv->mad;
	struct ib_mad_hdr *resp_hdr = (struct ib_mad_hdr *)response->mad;

	if (recv_hdr->method == IB_MGMT_METHOD_GET ||
	    recv_hdr->method == IB_MGMT_METHOD_SET) {
		memcpy(response, recv, mad_priv_size(response));
		response->header.recv_wc.wc = &response->header.wc;
		response->header.recv_wc.recv_buf.mad = (struct ib_mad *)response->mad;
		response->header.recv_wc.recv_buf.grh = &response->grh;
		resp_hdr->method = IB_MGMT_METHOD_GET_RESP;
		resp_hdr->status = cpu_to_be16(IB_MGMT_MAD_STATUS_UNSUPPORTED_METHOD_ATTRIB);
		if (recv_hdr->mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
			resp_hdr->status |= IB_SMP_DIRECTION;

		if (opa && recv_hdr->base_version == OPA_MGMT_BASE_VERSION) {
			if (recv_hdr->mgmt_class ==
			    IB_MGMT_CLASS_SUBN_LID_ROUTED ||
			    recv_hdr->mgmt_class ==
			    IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
				*resp_len = opa_get_smp_header_size(
							(struct opa_smp *)recv->mad);
			else
				*resp_len = sizeof(struct ib_mad_hdr);
		}

		return true;
	} else {
		return false;
	}
}

static enum smi_action
handle_opa_smi(struct ib_mad_port_private *port_priv,
	       struct ib_mad_qp_info *qp_info,
	       struct ib_wc *wc,
	       int port_num,
	       struct ib_mad_private *recv,
	       struct ib_mad_private *response)
{
	enum smi_forward_action retsmi;
	struct opa_smp *smp = (struct opa_smp *)recv->mad;

	trace_ib_mad_handle_opa_smi(smp);

	if (opa_smi_handle_dr_smp_recv(smp,
				   rdma_cap_ib_switch(port_priv->device),
				   port_num,
				   port_priv->device->phys_port_cnt) ==
				   IB_SMI_DISCARD)
		return IB_SMI_DISCARD;

	retsmi = opa_smi_check_forward_dr_smp(smp);
	if (retsmi == IB_SMI_LOCAL)
		return IB_SMI_HANDLE;

	if (retsmi == IB_SMI_SEND) { /* don't forward */
		if (opa_smi_handle_dr_smp_send(smp,
					   rdma_cap_ib_switch(port_priv->device),
					   port_num) == IB_SMI_DISCARD)
			return IB_SMI_DISCARD;

		if (opa_smi_check_local_smp(smp, port_priv->device) ==
		    IB_SMI_DISCARD)
			return IB_SMI_DISCARD;

	} else if (rdma_cap_ib_switch(port_priv->device)) {
		/* forward case for switches */
		memcpy(response, recv, mad_priv_size(response));
		response->header.recv_wc.wc = &response->header.wc;
		response->header.recv_wc.recv_buf.opa_mad =
				(struct opa_mad *)response->mad;
		response->header.recv_wc.recv_buf.grh = &response->grh;

		agent_send_response((const struct ib_mad_hdr *)response->mad,
				    &response->grh, wc,
				    port_priv->device,
				    opa_smi_get_fwd_port(smp),
				    qp_info->qp->qp_num,
				    recv->header.wc.byte_len,
				    true);

		return IB_SMI_DISCARD;
	}

	return IB_SMI_HANDLE;
}

static enum smi_action
handle_smi(struct ib_mad_port_private *port_priv,
	   struct ib_mad_qp_info *qp_info,
	   struct ib_wc *wc,
	   int port_num,
	   struct ib_mad_private *recv,
	   struct ib_mad_private *response,
	   bool opa)
{
	struct ib_mad_hdr *mad_hdr = (struct ib_mad_hdr *)recv->mad;

	if (opa && mad_hdr->base_version == OPA_MGMT_BASE_VERSION &&
	    mad_hdr->class_version == OPA_SM_CLASS_VERSION)
		return handle_opa_smi(port_priv, qp_info, wc, port_num, recv,
				      response);

	return handle_ib_smi(port_priv, qp_info, wc, port_num, recv, response);
}

static void ib_mad_recv_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_mad_port_private *port_priv = cq->cq_context;
	struct ib_mad_list_head *mad_list =
		container_of(wc->wr_cqe, struct ib_mad_list_head, cqe);
	struct ib_mad_qp_info *qp_info;
	struct ib_mad_private_header *mad_priv_hdr;
	struct ib_mad_private *recv, *response = NULL;
	struct ib_mad_agent_private *mad_agent;
	int port_num;
	int ret = IB_MAD_RESULT_SUCCESS;
	size_t mad_size;
	u16 resp_mad_pkey_index = 0;
	bool opa;

	if (list_empty_careful(&port_priv->port_list))
		return;

	if (wc->status != IB_WC_SUCCESS) {
		/*
		 * Receive errors indicate that the QP has entered the error
		 * state - error handling/shutdown code will cleanup
		 */
		return;
	}

	qp_info = mad_list->mad_queue->qp_info;
	dequeue_mad(mad_list);

	opa = rdma_cap_opa_mad(qp_info->port_priv->device,
			       qp_info->port_priv->port_num);

	mad_priv_hdr = container_of(mad_list, struct ib_mad_private_header,
				    mad_list);
	recv = container_of(mad_priv_hdr, struct ib_mad_private, header);
	ib_dma_unmap_single(port_priv->device,
			    recv->header.mapping,
			    mad_priv_dma_size(recv),
			    DMA_FROM_DEVICE);

	/* Setup MAD receive work completion from "normal" work completion */
	recv->header.wc = *wc;
	recv->header.recv_wc.wc = &recv->header.wc;

	if (opa && ((struct ib_mad_hdr *)(recv->mad))->base_version == OPA_MGMT_BASE_VERSION) {
		recv->header.recv_wc.mad_len = wc->byte_len - sizeof(struct ib_grh);
		recv->header.recv_wc.mad_seg_size = sizeof(struct opa_mad);
	} else {
		recv->header.recv_wc.mad_len = sizeof(struct ib_mad);
		recv->header.recv_wc.mad_seg_size = sizeof(struct ib_mad);
	}

	recv->header.recv_wc.recv_buf.mad = (struct ib_mad *)recv->mad;
	recv->header.recv_wc.recv_buf.grh = &recv->grh;

	/* Validate MAD */
	if (!validate_mad((const struct ib_mad_hdr *)recv->mad, qp_info, opa))
		goto out;

	trace_ib_mad_recv_done_handler(qp_info, wc,
				       (struct ib_mad_hdr *)recv->mad);

	mad_size = recv->mad_size;
	response = alloc_mad_private(mad_size, GFP_KERNEL);
	if (!response)
		goto out;

	if (rdma_cap_ib_switch(port_priv->device))
		port_num = wc->port_num;
	else
		port_num = port_priv->port_num;

	if (((struct ib_mad_hdr *)recv->mad)->mgmt_class ==
	    IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) {
		if (handle_smi(port_priv, qp_info, wc, port_num, recv,
			       response, opa)
		    == IB_SMI_DISCARD)
			goto out;
	}

	/* Give driver "right of first refusal" on incoming MAD */
	if (port_priv->device->ops.process_mad) {
		ret = port_priv->device->ops.process_mad(
			port_priv->device, 0, port_priv->port_num, wc,
			&recv->grh, (const struct ib_mad *)recv->mad,
			(struct ib_mad *)response->mad, &mad_size,
			&resp_mad_pkey_index);

		if (opa)
			wc->pkey_index = resp_mad_pkey_index;

		if (ret & IB_MAD_RESULT_SUCCESS) {
			if (ret & IB_MAD_RESULT_CONSUMED)
				goto out;
			if (ret & IB_MAD_RESULT_REPLY) {
				agent_send_response((const struct ib_mad_hdr *)response->mad,
						    &recv->grh, wc,
						    port_priv->device,
						    port_num,
						    qp_info->qp->qp_num,
						    mad_size, opa);
				goto out;
			}
		}
	}

	mad_agent = find_mad_agent(port_priv, (const struct ib_mad_hdr *)recv->mad);
	if (mad_agent) {
		trace_ib_mad_recv_done_agent(mad_agent);
		ib_mad_complete_recv(mad_agent, &recv->header.recv_wc);
		/*
		 * recv is freed up in error cases in ib_mad_complete_recv
		 * or via recv_handler in ib_mad_complete_recv()
		 */
		recv = NULL;
	} else if ((ret & IB_MAD_RESULT_SUCCESS) &&
		   generate_unmatched_resp(recv, response, &mad_size, opa)) {
		agent_send_response((const struct ib_mad_hdr *)response->mad, &recv->grh, wc,
				    port_priv->device, port_num,
				    qp_info->qp->qp_num, mad_size, opa);
	}

out:
	/* Post another receive request for this QP */
	if (response) {
		ib_mad_post_receive_mads(qp_info, response);
		kfree(recv);
	} else
		ib_mad_post_receive_mads(qp_info, recv);
}

static void adjust_timeout(struct ib_mad_agent_private *mad_agent_priv)
{
	struct ib_mad_send_wr_private *mad_send_wr;
	unsigned long delay;

	if (list_empty(&mad_agent_priv->wait_list)) {
		cancel_delayed_work(&mad_agent_priv->timed_work);
	} else {
		mad_send_wr = list_entry(mad_agent_priv->wait_list.next,
					 struct ib_mad_send_wr_private,
					 agent_list);

		if (time_after(mad_agent_priv->timeout,
			       mad_send_wr->timeout)) {
			mad_agent_priv->timeout = mad_send_wr->timeout;
			delay = mad_send_wr->timeout - jiffies;
			if ((long)delay <= 0)
				delay = 1;
			mod_delayed_work(mad_agent_priv->qp_info->port_priv->wq,
					 &mad_agent_priv->timed_work, delay);
		}
	}
}

static void wait_for_response(struct ib_mad_send_wr_private *mad_send_wr)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_send_wr_private *temp_mad_send_wr;
	struct list_head *list_item;
	unsigned long delay;

	mad_agent_priv = mad_send_wr->mad_agent_priv;
	list_del(&mad_send_wr->agent_list);

	delay = mad_send_wr->timeout;
	mad_send_wr->timeout += jiffies;

	if (delay) {
		list_for_each_prev(list_item, &mad_agent_priv->wait_list) {
			temp_mad_send_wr = list_entry(list_item,
						struct ib_mad_send_wr_private,
						agent_list);
			if (time_after(mad_send_wr->timeout,
				       temp_mad_send_wr->timeout))
				break;
		}
	}
	else
		list_item = &mad_agent_priv->wait_list;
	list_add(&mad_send_wr->agent_list, list_item);

	/* Reschedule a work item if we have a shorter timeout */
	if (mad_agent_priv->wait_list.next == &mad_send_wr->agent_list)
		mod_delayed_work(mad_agent_priv->qp_info->port_priv->wq,
				 &mad_agent_priv->timed_work, delay);
}

void ib_reset_mad_timeout(struct ib_mad_send_wr_private *mad_send_wr,
			  unsigned long timeout_ms)
{
	mad_send_wr->timeout = msecs_to_jiffies(timeout_ms);
	wait_for_response(mad_send_wr);
}

/*
 * Process a send work completion
 */
void ib_mad_complete_send_wr(struct ib_mad_send_wr_private *mad_send_wr,
			     struct ib_mad_send_wc *mad_send_wc)
{
	struct ib_mad_agent_private	*mad_agent_priv;
	unsigned long			flags;
	int				ret;

	mad_agent_priv = mad_send_wr->mad_agent_priv;
	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	if (ib_mad_kernel_rmpp_agent(&mad_agent_priv->agent)) {
		ret = ib_process_rmpp_send_wc(mad_send_wr, mad_send_wc);
		if (ret == IB_RMPP_RESULT_CONSUMED)
			goto done;
	} else
		ret = IB_RMPP_RESULT_UNHANDLED;

	if (mad_send_wc->status != IB_WC_SUCCESS &&
	    mad_send_wr->status == IB_WC_SUCCESS) {
		mad_send_wr->status = mad_send_wc->status;
		mad_send_wr->refcount -= (mad_send_wr->timeout > 0);
	}

	if (--mad_send_wr->refcount > 0) {
		if (mad_send_wr->refcount == 1 && mad_send_wr->timeout &&
		    mad_send_wr->status == IB_WC_SUCCESS) {
			wait_for_response(mad_send_wr);
		}
		goto done;
	}

	/* Remove send from MAD agent and notify client of completion */
	list_del(&mad_send_wr->agent_list);
	adjust_timeout(mad_agent_priv);
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

	if (mad_send_wr->status != IB_WC_SUCCESS )
		mad_send_wc->status = mad_send_wr->status;
	if (ret == IB_RMPP_RESULT_INTERNAL)
		ib_rmpp_send_handler(mad_send_wc);
	else
		mad_agent_priv->agent.send_handler(&mad_agent_priv->agent,
						   mad_send_wc);

	/* Release reference on agent taken when sending */
	deref_mad_agent(mad_agent_priv);
	return;
done:
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
}

static void ib_mad_send_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_mad_port_private *port_priv = cq->cq_context;
	struct ib_mad_list_head *mad_list =
		container_of(wc->wr_cqe, struct ib_mad_list_head, cqe);
	struct ib_mad_send_wr_private	*mad_send_wr, *queued_send_wr;
	struct ib_mad_qp_info		*qp_info;
	struct ib_mad_queue		*send_queue;
	struct ib_mad_send_wc		mad_send_wc;
	unsigned long flags;
	int ret;

	if (list_empty_careful(&port_priv->port_list))
		return;

	if (wc->status != IB_WC_SUCCESS) {
		if (!ib_mad_send_error(port_priv, wc))
			return;
	}

	mad_send_wr = container_of(mad_list, struct ib_mad_send_wr_private,
				   mad_list);
	send_queue = mad_list->mad_queue;
	qp_info = send_queue->qp_info;

	trace_ib_mad_send_done_agent(mad_send_wr->mad_agent_priv);
	trace_ib_mad_send_done_handler(mad_send_wr, wc);

retry:
	ib_dma_unmap_single(mad_send_wr->send_buf.mad_agent->device,
			    mad_send_wr->header_mapping,
			    mad_send_wr->sg_list[0].length, DMA_TO_DEVICE);
	ib_dma_unmap_single(mad_send_wr->send_buf.mad_agent->device,
			    mad_send_wr->payload_mapping,
			    mad_send_wr->sg_list[1].length, DMA_TO_DEVICE);
	queued_send_wr = NULL;
	spin_lock_irqsave(&send_queue->lock, flags);
	list_del(&mad_list->list);

	/* Move queued send to the send queue */
	if (send_queue->count-- > send_queue->max_active) {
		mad_list = container_of(qp_info->overflow_list.next,
					struct ib_mad_list_head, list);
		queued_send_wr = container_of(mad_list,
					struct ib_mad_send_wr_private,
					mad_list);
		list_move_tail(&mad_list->list, &send_queue->list);
	}
	spin_unlock_irqrestore(&send_queue->lock, flags);

	mad_send_wc.send_buf = &mad_send_wr->send_buf;
	mad_send_wc.status = wc->status;
	mad_send_wc.vendor_err = wc->vendor_err;
	ib_mad_complete_send_wr(mad_send_wr, &mad_send_wc);

	if (queued_send_wr) {
		trace_ib_mad_send_done_resend(queued_send_wr, qp_info);
		ret = ib_post_send(qp_info->qp, &queued_send_wr->send_wr.wr,
				   NULL);
		if (ret) {
			dev_err(&port_priv->device->dev,
				"ib_post_send failed: %d\n", ret);
			mad_send_wr = queued_send_wr;
			wc->status = IB_WC_LOC_QP_OP_ERR;
			goto retry;
		}
	}
}

static void mark_sends_for_retry(struct ib_mad_qp_info *qp_info)
{
	struct ib_mad_send_wr_private *mad_send_wr;
	struct ib_mad_list_head *mad_list;
	unsigned long flags;

	spin_lock_irqsave(&qp_info->send_queue.lock, flags);
	list_for_each_entry(mad_list, &qp_info->send_queue.list, list) {
		mad_send_wr = container_of(mad_list,
					   struct ib_mad_send_wr_private,
					   mad_list);
		mad_send_wr->retry = 1;
	}
	spin_unlock_irqrestore(&qp_info->send_queue.lock, flags);
}

static bool ib_mad_send_error(struct ib_mad_port_private *port_priv,
		struct ib_wc *wc)
{
	struct ib_mad_list_head *mad_list =
		container_of(wc->wr_cqe, struct ib_mad_list_head, cqe);
	struct ib_mad_qp_info *qp_info = mad_list->mad_queue->qp_info;
	struct ib_mad_send_wr_private *mad_send_wr;
	int ret;

	/*
	 * Send errors will transition the QP to SQE - move
	 * QP to RTS and repost flushed work requests
	 */
	mad_send_wr = container_of(mad_list, struct ib_mad_send_wr_private,
				   mad_list);
	if (wc->status == IB_WC_WR_FLUSH_ERR) {
		if (mad_send_wr->retry) {
			/* Repost send */
			mad_send_wr->retry = 0;
			trace_ib_mad_error_handler(mad_send_wr, qp_info);
			ret = ib_post_send(qp_info->qp, &mad_send_wr->send_wr.wr,
					   NULL);
			if (!ret)
				return false;
		}
	} else {
		struct ib_qp_attr *attr;

		/* Transition QP to RTS and fail offending send */
		attr = kmalloc(sizeof *attr, GFP_KERNEL);
		if (attr) {
			attr->qp_state = IB_QPS_RTS;
			attr->cur_qp_state = IB_QPS_SQE;
			ret = ib_modify_qp(qp_info->qp, attr,
					   IB_QP_STATE | IB_QP_CUR_STATE);
			kfree(attr);
			if (ret)
				dev_err(&port_priv->device->dev,
					"%s - ib_modify_qp to RTS: %d\n",
					__func__, ret);
			else
				mark_sends_for_retry(qp_info);
		}
	}

	return true;
}

static void cancel_mads(struct ib_mad_agent_private *mad_agent_priv)
{
	unsigned long flags;
	struct ib_mad_send_wr_private *mad_send_wr, *temp_mad_send_wr;
	struct ib_mad_send_wc mad_send_wc;
	struct list_head cancel_list;

	INIT_LIST_HEAD(&cancel_list);

	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	list_for_each_entry_safe(mad_send_wr, temp_mad_send_wr,
				 &mad_agent_priv->send_list, agent_list) {
		if (mad_send_wr->status == IB_WC_SUCCESS) {
			mad_send_wr->status = IB_WC_WR_FLUSH_ERR;
			mad_send_wr->refcount -= (mad_send_wr->timeout > 0);
		}
	}

	/* Empty wait list to prevent receives from finding a request */
	list_splice_init(&mad_agent_priv->wait_list, &cancel_list);
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

	/* Report all cancelled requests */
	mad_send_wc.status = IB_WC_WR_FLUSH_ERR;
	mad_send_wc.vendor_err = 0;

	list_for_each_entry_safe(mad_send_wr, temp_mad_send_wr,
				 &cancel_list, agent_list) {
		mad_send_wc.send_buf = &mad_send_wr->send_buf;
		list_del(&mad_send_wr->agent_list);
		mad_agent_priv->agent.send_handler(&mad_agent_priv->agent,
						   &mad_send_wc);
		atomic_dec(&mad_agent_priv->refcount);
	}
}

static struct ib_mad_send_wr_private*
find_send_wr(struct ib_mad_agent_private *mad_agent_priv,
	     struct ib_mad_send_buf *send_buf)
{
	struct ib_mad_send_wr_private *mad_send_wr;

	list_for_each_entry(mad_send_wr, &mad_agent_priv->wait_list,
			    agent_list) {
		if (&mad_send_wr->send_buf == send_buf)
			return mad_send_wr;
	}

	list_for_each_entry(mad_send_wr, &mad_agent_priv->send_list,
			    agent_list) {
		if (is_rmpp_data_mad(mad_agent_priv,
				     mad_send_wr->send_buf.mad) &&
		    &mad_send_wr->send_buf == send_buf)
			return mad_send_wr;
	}
	return NULL;
}

int ib_modify_mad(struct ib_mad_agent *mad_agent,
		  struct ib_mad_send_buf *send_buf, u32 timeout_ms)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_send_wr_private *mad_send_wr;
	unsigned long flags;
	int active;

	mad_agent_priv = container_of(mad_agent, struct ib_mad_agent_private,
				      agent);
	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	mad_send_wr = find_send_wr(mad_agent_priv, send_buf);
	if (!mad_send_wr || mad_send_wr->status != IB_WC_SUCCESS) {
		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
		return -EINVAL;
	}

	active = (!mad_send_wr->timeout || mad_send_wr->refcount > 1);
	if (!timeout_ms) {
		mad_send_wr->status = IB_WC_WR_FLUSH_ERR;
		mad_send_wr->refcount -= (mad_send_wr->timeout > 0);
	}

	mad_send_wr->send_buf.timeout_ms = timeout_ms;
	if (active)
		mad_send_wr->timeout = msecs_to_jiffies(timeout_ms);
	else
		ib_reset_mad_timeout(mad_send_wr, timeout_ms);

	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
	return 0;
}
EXPORT_SYMBOL(ib_modify_mad);

void ib_cancel_mad(struct ib_mad_agent *mad_agent,
		   struct ib_mad_send_buf *send_buf)
{
	ib_modify_mad(mad_agent, send_buf, 0);
}
EXPORT_SYMBOL(ib_cancel_mad);

static void local_completions(struct work_struct *work)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_local_private *local;
	struct ib_mad_agent_private *recv_mad_agent;
	unsigned long flags;
	int free_mad;
	struct ib_wc wc;
	struct ib_mad_send_wc mad_send_wc;
	bool opa;

	mad_agent_priv =
		container_of(work, struct ib_mad_agent_private, local_work);

	opa = rdma_cap_opa_mad(mad_agent_priv->qp_info->port_priv->device,
			       mad_agent_priv->qp_info->port_priv->port_num);

	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	while (!list_empty(&mad_agent_priv->local_list)) {
		local = list_entry(mad_agent_priv->local_list.next,
				   struct ib_mad_local_private,
				   completion_list);
		list_del(&local->completion_list);
		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
		free_mad = 0;
		if (local->mad_priv) {
			u8 base_version;
			recv_mad_agent = local->recv_mad_agent;
			if (!recv_mad_agent) {
				dev_err(&mad_agent_priv->agent.device->dev,
					"No receive MAD agent for local completion\n");
				free_mad = 1;
				goto local_send_completion;
			}

			/*
			 * Defined behavior is to complete response
			 * before request
			 */
			build_smp_wc(recv_mad_agent->agent.qp,
				     local->mad_send_wr->send_wr.wr.wr_cqe,
				     be16_to_cpu(IB_LID_PERMISSIVE),
				     local->mad_send_wr->send_wr.pkey_index,
				     recv_mad_agent->agent.port_num, &wc);

			local->mad_priv->header.recv_wc.wc = &wc;

			base_version = ((struct ib_mad_hdr *)(local->mad_priv->mad))->base_version;
			if (opa && base_version == OPA_MGMT_BASE_VERSION) {
				local->mad_priv->header.recv_wc.mad_len = local->return_wc_byte_len;
				local->mad_priv->header.recv_wc.mad_seg_size = sizeof(struct opa_mad);
			} else {
				local->mad_priv->header.recv_wc.mad_len = sizeof(struct ib_mad);
				local->mad_priv->header.recv_wc.mad_seg_size = sizeof(struct ib_mad);
			}

			INIT_LIST_HEAD(&local->mad_priv->header.recv_wc.rmpp_list);
			list_add(&local->mad_priv->header.recv_wc.recv_buf.list,
				 &local->mad_priv->header.recv_wc.rmpp_list);
			local->mad_priv->header.recv_wc.recv_buf.grh = NULL;
			local->mad_priv->header.recv_wc.recv_buf.mad =
						(struct ib_mad *)local->mad_priv->mad;
			recv_mad_agent->agent.recv_handler(
						&recv_mad_agent->agent,
						&local->mad_send_wr->send_buf,
						&local->mad_priv->header.recv_wc);
			spin_lock_irqsave(&recv_mad_agent->lock, flags);
			atomic_dec(&recv_mad_agent->refcount);
			spin_unlock_irqrestore(&recv_mad_agent->lock, flags);
		}

local_send_completion:
		/* Complete send */
		mad_send_wc.status = IB_WC_SUCCESS;
		mad_send_wc.vendor_err = 0;
		mad_send_wc.send_buf = &local->mad_send_wr->send_buf;
		mad_agent_priv->agent.send_handler(&mad_agent_priv->agent,
						   &mad_send_wc);

		spin_lock_irqsave(&mad_agent_priv->lock, flags);
		atomic_dec(&mad_agent_priv->refcount);
		if (free_mad)
			kfree(local->mad_priv);
		kfree(local);
	}
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
}

static int retry_send(struct ib_mad_send_wr_private *mad_send_wr)
{
	int ret;

	if (!mad_send_wr->retries_left)
		return -ETIMEDOUT;

	mad_send_wr->retries_left--;
	mad_send_wr->send_buf.retries++;

	mad_send_wr->timeout = msecs_to_jiffies(mad_send_wr->send_buf.timeout_ms);

	if (ib_mad_kernel_rmpp_agent(&mad_send_wr->mad_agent_priv->agent)) {
		ret = ib_retry_rmpp(mad_send_wr);
		switch (ret) {
		case IB_RMPP_RESULT_UNHANDLED:
			ret = ib_send_mad(mad_send_wr);
			break;
		case IB_RMPP_RESULT_CONSUMED:
			ret = 0;
			break;
		default:
			ret = -ECOMM;
			break;
		}
	} else
		ret = ib_send_mad(mad_send_wr);

	if (!ret) {
		mad_send_wr->refcount++;
		list_add_tail(&mad_send_wr->agent_list,
			      &mad_send_wr->mad_agent_priv->send_list);
	}
	return ret;
}

static void timeout_sends(struct work_struct *work)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_send_wr_private *mad_send_wr;
	struct ib_mad_send_wc mad_send_wc;
	unsigned long flags, delay;

	mad_agent_priv = container_of(work, struct ib_mad_agent_private,
				      timed_work.work);
	mad_send_wc.vendor_err = 0;

	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	while (!list_empty(&mad_agent_priv->wait_list)) {
		mad_send_wr = list_entry(mad_agent_priv->wait_list.next,
					 struct ib_mad_send_wr_private,
					 agent_list);

		if (time_after(mad_send_wr->timeout, jiffies)) {
			delay = mad_send_wr->timeout - jiffies;
			if ((long)delay <= 0)
				delay = 1;
			queue_delayed_work(mad_agent_priv->qp_info->
					   port_priv->wq,
					   &mad_agent_priv->timed_work, delay);
			break;
		}

		list_del(&mad_send_wr->agent_list);
		if (mad_send_wr->status == IB_WC_SUCCESS &&
		    !retry_send(mad_send_wr))
			continue;

		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

		if (mad_send_wr->status == IB_WC_SUCCESS)
			mad_send_wc.status = IB_WC_RESP_TIMEOUT_ERR;
		else
			mad_send_wc.status = mad_send_wr->status;
		mad_send_wc.send_buf = &mad_send_wr->send_buf;
		mad_agent_priv->agent.send_handler(&mad_agent_priv->agent,
						   &mad_send_wc);

		atomic_dec(&mad_agent_priv->refcount);
		spin_lock_irqsave(&mad_agent_priv->lock, flags);
	}
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
}

/*
 * Allocate receive MADs and post receive WRs for them
 */
static int ib_mad_post_receive_mads(struct ib_mad_qp_info *qp_info,
				    struct ib_mad_private *mad)
{
	unsigned long flags;
	int post, ret;
	struct ib_mad_private *mad_priv;
	struct ib_sge sg_list;
	struct ib_recv_wr recv_wr;
	struct ib_mad_queue *recv_queue = &qp_info->recv_queue;

	/* Initialize common scatter list fields */
	sg_list.lkey = qp_info->port_priv->pd->local_dma_lkey;

	/* Initialize common receive WR fields */
	recv_wr.next = NULL;
	recv_wr.sg_list = &sg_list;
	recv_wr.num_sge = 1;

	do {
		/* Allocate and map receive buffer */
		if (mad) {
			mad_priv = mad;
			mad = NULL;
		} else {
			mad_priv = alloc_mad_private(port_mad_size(qp_info->port_priv),
						     GFP_ATOMIC);
			if (!mad_priv) {
				ret = -ENOMEM;
				break;
			}
		}
		sg_list.length = mad_priv_dma_size(mad_priv);
		sg_list.addr = ib_dma_map_single(qp_info->port_priv->device,
						 &mad_priv->grh,
						 mad_priv_dma_size(mad_priv),
						 DMA_FROM_DEVICE);
		if (unlikely(ib_dma_mapping_error(qp_info->port_priv->device,
						  sg_list.addr))) {
			kfree(mad_priv);
			ret = -ENOMEM;
			break;
		}
		mad_priv->header.mapping = sg_list.addr;
		mad_priv->header.mad_list.mad_queue = recv_queue;
		mad_priv->header.mad_list.cqe.done = ib_mad_recv_done;
		recv_wr.wr_cqe = &mad_priv->header.mad_list.cqe;

		/* Post receive WR */
		spin_lock_irqsave(&recv_queue->lock, flags);
		post = (++recv_queue->count < recv_queue->max_active);
		list_add_tail(&mad_priv->header.mad_list.list, &recv_queue->list);
		spin_unlock_irqrestore(&recv_queue->lock, flags);
		ret = ib_post_recv(qp_info->qp, &recv_wr, NULL);
		if (ret) {
			spin_lock_irqsave(&recv_queue->lock, flags);
			list_del(&mad_priv->header.mad_list.list);
			recv_queue->count--;
			spin_unlock_irqrestore(&recv_queue->lock, flags);
			ib_dma_unmap_single(qp_info->port_priv->device,
					    mad_priv->header.mapping,
					    mad_priv_dma_size(mad_priv),
					    DMA_FROM_DEVICE);
			kfree(mad_priv);
			dev_err(&qp_info->port_priv->device->dev,
				"ib_post_recv failed: %d\n", ret);
			break;
		}
	} while (post);

	return ret;
}

/*
 * Return all the posted receive MADs
 */
static void cleanup_recv_queue(struct ib_mad_qp_info *qp_info)
{
	struct ib_mad_private_header *mad_priv_hdr;
	struct ib_mad_private *recv;
	struct ib_mad_list_head *mad_list;

	if (!qp_info->qp)
		return;

	while (!list_empty(&qp_info->recv_queue.list)) {

		mad_list = list_entry(qp_info->recv_queue.list.next,
				      struct ib_mad_list_head, list);
		mad_priv_hdr = container_of(mad_list,
					    struct ib_mad_private_header,
					    mad_list);
		recv = container_of(mad_priv_hdr, struct ib_mad_private,
				    header);

		/* Remove from posted receive MAD list */
		list_del(&mad_list->list);

		ib_dma_unmap_single(qp_info->port_priv->device,
				    recv->header.mapping,
				    mad_priv_dma_size(recv),
				    DMA_FROM_DEVICE);
		kfree(recv);
	}

	qp_info->recv_queue.count = 0;
}

/*
 * Start the port
 */
static int ib_mad_port_start(struct ib_mad_port_private *port_priv)
{
	int ret, i;
	struct ib_qp_attr *attr;
	struct ib_qp *qp;
	u16 pkey_index;

	attr = kmalloc(sizeof *attr, GFP_KERNEL);
	if (!attr)
		return -ENOMEM;

	ret = ib_find_pkey(port_priv->device, port_priv->port_num,
			   IB_DEFAULT_PKEY_FULL, &pkey_index);
	if (ret)
		pkey_index = 0;

	for (i = 0; i < IB_MAD_QPS_CORE; i++) {
		qp = port_priv->qp_info[i].qp;
		if (!qp)
			continue;

		/*
		 * PKey index for QP1 is irrelevant but
		 * one is needed for the Reset to Init transition
		 */
		attr->qp_state = IB_QPS_INIT;
		attr->pkey_index = pkey_index;
		attr->qkey = (qp->qp_num == 0) ? 0 : IB_QP1_QKEY;
		ret = ib_modify_qp(qp, attr, IB_QP_STATE |
					     IB_QP_PKEY_INDEX | IB_QP_QKEY);
		if (ret) {
			dev_err(&port_priv->device->dev,
				"Couldn't change QP%d state to INIT: %d\n",
				i, ret);
			goto out;
		}

		attr->qp_state = IB_QPS_RTR;
		ret = ib_modify_qp(qp, attr, IB_QP_STATE);
		if (ret) {
			dev_err(&port_priv->device->dev,
				"Couldn't change QP%d state to RTR: %d\n",
				i, ret);
			goto out;
		}

		attr->qp_state = IB_QPS_RTS;
		attr->sq_psn = IB_MAD_SEND_Q_PSN;
		ret = ib_modify_qp(qp, attr, IB_QP_STATE | IB_QP_SQ_PSN);
		if (ret) {
			dev_err(&port_priv->device->dev,
				"Couldn't change QP%d state to RTS: %d\n",
				i, ret);
			goto out;
		}
	}

	ret = ib_req_notify_cq(port_priv->cq, IB_CQ_NEXT_COMP);
	if (ret) {
		dev_err(&port_priv->device->dev,
			"Failed to request completion notification: %d\n",
			ret);
		goto out;
	}

	for (i = 0; i < IB_MAD_QPS_CORE; i++) {
		if (!port_priv->qp_info[i].qp)
			continue;

		ret = ib_mad_post_receive_mads(&port_priv->qp_info[i], NULL);
		if (ret) {
			dev_err(&port_priv->device->dev,
				"Couldn't post receive WRs\n");
			goto out;
		}
	}
out:
	kfree(attr);
	return ret;
}

static void qp_event_handler(struct ib_event *event, void *qp_context)
{
	struct ib_mad_qp_info	*qp_info = qp_context;

	/* It's worse than that! He's dead, Jim! */
	dev_err(&qp_info->port_priv->device->dev,
		"Fatal error (%d) on MAD QP (%d)\n",
		event->event, qp_info->qp->qp_num);
}

static void init_mad_queue(struct ib_mad_qp_info *qp_info,
			   struct ib_mad_queue *mad_queue)
{
	mad_queue->qp_info = qp_info;
	mad_queue->count = 0;
	spin_lock_init(&mad_queue->lock);
	INIT_LIST_HEAD(&mad_queue->list);
}

static void init_mad_qp(struct ib_mad_port_private *port_priv,
			struct ib_mad_qp_info *qp_info)
{
	qp_info->port_priv = port_priv;
	init_mad_queue(qp_info, &qp_info->send_queue);
	init_mad_queue(qp_info, &qp_info->recv_queue);
	INIT_LIST_HEAD(&qp_info->overflow_list);
}

static int create_mad_qp(struct ib_mad_qp_info *qp_info,
			 enum ib_qp_type qp_type)
{
	struct ib_qp_init_attr	qp_init_attr;
	int ret;

	memset(&qp_init_attr, 0, sizeof qp_init_attr);
	qp_init_attr.send_cq = qp_info->port_priv->cq;
	qp_init_attr.recv_cq = qp_info->port_priv->cq;
	qp_init_attr.sq_sig_type = IB_SIGNAL_ALL_WR;
	qp_init_attr.cap.max_send_wr = mad_sendq_size;
	qp_init_attr.cap.max_recv_wr = mad_recvq_size;
	qp_init_attr.cap.max_send_sge = IB_MAD_SEND_REQ_MAX_SG;
	qp_init_attr.cap.max_recv_sge = IB_MAD_RECV_REQ_MAX_SG;
	qp_init_attr.qp_type = qp_type;
	qp_init_attr.port_num = qp_info->port_priv->port_num;
	qp_init_attr.qp_context = qp_info;
	qp_init_attr.event_handler = qp_event_handler;
	qp_info->qp = ib_create_qp(qp_info->port_priv->pd, &qp_init_attr);
	if (IS_ERR(qp_info->qp)) {
		dev_err(&qp_info->port_priv->device->dev,
			"Couldn't create ib_mad QP%d\n",
			get_spl_qp_index(qp_type));
		ret = PTR_ERR(qp_info->qp);
		goto error;
	}
	/* Use minimum queue sizes unless the CQ is resized */
	qp_info->send_queue.max_active = mad_sendq_size;
	qp_info->recv_queue.max_active = mad_recvq_size;
	return 0;

error:
	return ret;
}

static void destroy_mad_qp(struct ib_mad_qp_info *qp_info)
{
	if (!qp_info->qp)
		return;

	ib_destroy_qp(qp_info->qp);
}

/*
 * Open the port
 * Create the QP, PD, MR, and CQ if needed
 */
static int ib_mad_port_open(struct ib_device *device,
			    int port_num)
{
	int ret, cq_size;
	struct ib_mad_port_private *port_priv;
	unsigned long flags;
	char name[sizeof "ib_mad123"];
	int has_smi;

	if (WARN_ON(rdma_max_mad_size(device, port_num) < IB_MGMT_MAD_SIZE))
		return -EFAULT;

	if (WARN_ON(rdma_cap_opa_mad(device, port_num) &&
		    rdma_max_mad_size(device, port_num) < OPA_MGMT_MAD_SIZE))
		return -EFAULT;

	/* Create new device info */
	port_priv = kzalloc(sizeof *port_priv, GFP_KERNEL);
	if (!port_priv)
		return -ENOMEM;

	port_priv->device = device;
	port_priv->port_num = port_num;
	spin_lock_init(&port_priv->reg_lock);
	init_mad_qp(port_priv, &port_priv->qp_info[0]);
	init_mad_qp(port_priv, &port_priv->qp_info[1]);

	cq_size = mad_sendq_size + mad_recvq_size;
	has_smi = rdma_cap_ib_smi(device, port_num);
	if (has_smi)
		cq_size *= 2;

	port_priv->pd = ib_alloc_pd(device, 0);
	if (IS_ERR(port_priv->pd)) {
		dev_err(&device->dev, "Couldn't create ib_mad PD\n");
		ret = PTR_ERR(port_priv->pd);
		goto error3;
	}

	port_priv->cq = ib_alloc_cq(port_priv->device, port_priv, cq_size, 0,
			IB_POLL_UNBOUND_WORKQUEUE);
	if (IS_ERR(port_priv->cq)) {
		dev_err(&device->dev, "Couldn't create ib_mad CQ\n");
		ret = PTR_ERR(port_priv->cq);
		goto error4;
	}

	if (has_smi) {
		ret = create_mad_qp(&port_priv->qp_info[0], IB_QPT_SMI);
		if (ret)
			goto error6;
	}
	ret = create_mad_qp(&port_priv->qp_info[1], IB_QPT_GSI);
	if (ret)
		goto error7;

	snprintf(name, sizeof name, "ib_mad%d", port_num);
	port_priv->wq = alloc_ordered_workqueue(name, WQ_MEM_RECLAIM);
	if (!port_priv->wq) {
		ret = -ENOMEM;
		goto error8;
	}

	spin_lock_irqsave(&ib_mad_port_list_lock, flags);
	list_add_tail(&port_priv->port_list, &ib_mad_port_list);
	spin_unlock_irqrestore(&ib_mad_port_list_lock, flags);

	ret = ib_mad_port_start(port_priv);
	if (ret) {
		dev_err(&device->dev, "Couldn't start port\n");
		goto error9;
	}

	return 0;

error9:
	spin_lock_irqsave(&ib_mad_port_list_lock, flags);
	list_del_init(&port_priv->port_list);
	spin_unlock_irqrestore(&ib_mad_port_list_lock, flags);

	destroy_workqueue(port_priv->wq);
error8:
	destroy_mad_qp(&port_priv->qp_info[1]);
error7:
	destroy_mad_qp(&port_priv->qp_info[0]);
error6:
	ib_free_cq(port_priv->cq);
	cleanup_recv_queue(&port_priv->qp_info[1]);
	cleanup_recv_queue(&port_priv->qp_info[0]);
error4:
	ib_dealloc_pd(port_priv->pd);
error3:
	kfree(port_priv);

	return ret;
}

/*
 * Close the port
 * If there are no classes using the port, free the port
 * resources (CQ, MR, PD, QP) and remove the port's info structure
 */
static int ib_mad_port_close(struct ib_device *device, int port_num)
{
	struct ib_mad_port_private *port_priv;
	unsigned long flags;

	spin_lock_irqsave(&ib_mad_port_list_lock, flags);
	port_priv = __ib_get_mad_port(device, port_num);
	if (port_priv == NULL) {
		spin_unlock_irqrestore(&ib_mad_port_list_lock, flags);
		dev_err(&device->dev, "Port %d not found\n", port_num);
		return -ENODEV;
	}
	list_del_init(&port_priv->port_list);
	spin_unlock_irqrestore(&ib_mad_port_list_lock, flags);

	destroy_workqueue(port_priv->wq);
	destroy_mad_qp(&port_priv->qp_info[1]);
	destroy_mad_qp(&port_priv->qp_info[0]);
	ib_free_cq(port_priv->cq);
	ib_dealloc_pd(port_priv->pd);
	cleanup_recv_queue(&port_priv->qp_info[1]);
	cleanup_recv_queue(&port_priv->qp_info[0]);
	/* XXX: Handle deallocation of MAD registration tables */

	kfree(port_priv);

	return 0;
}

static int ib_mad_init_device(struct ib_device *device)
{
	int start, i;
	unsigned int count = 0;
	int ret;

	start = rdma_start_port(device);

	for (i = start; i <= rdma_end_port(device); i++) {
		if (!rdma_cap_ib_mad(device, i))
			continue;

		ret = ib_mad_port_open(device, i);
		if (ret) {
			dev_err(&device->dev, "Couldn't open port %d\n", i);
			goto error;
		}
		ret = ib_agent_port_open(device, i);
		if (ret) {
			dev_err(&device->dev,
				"Couldn't open port %d for agents\n", i);
			goto error_agent;
		}
		count++;
	}
	if (!count)
		return -EOPNOTSUPP;

	return 0;

error_agent:
	if (ib_mad_port_close(device, i))
		dev_err(&device->dev, "Couldn't close port %d\n", i);

error:
	while (--i >= start) {
		if (!rdma_cap_ib_mad(device, i))
			continue;

		if (ib_agent_port_close(device, i))
			dev_err(&device->dev,
				"Couldn't close port %d for agents\n", i);
		if (ib_mad_port_close(device, i))
			dev_err(&device->dev, "Couldn't close port %d\n", i);
	}
	return ret;
}

static void ib_mad_remove_device(struct ib_device *device, void *client_data)
{
	unsigned int i;

	rdma_for_each_port (device, i) {
		if (!rdma_cap_ib_mad(device, i))
			continue;

		if (ib_agent_port_close(device, i))
			dev_err(&device->dev,
				"Couldn't close port %d for agents\n", i);
		if (ib_mad_port_close(device, i))
			dev_err(&device->dev, "Couldn't close port %d\n", i);
	}
}

static struct ib_client mad_client = {
	.name   = "mad",
	.add = ib_mad_init_device,
	.remove = ib_mad_remove_device
};

int ib_mad_init(void)
{
	mad_recvq_size = min(mad_recvq_size, IB_MAD_QP_MAX_SIZE);
	mad_recvq_size = max(mad_recvq_size, IB_MAD_QP_MIN_SIZE);

	mad_sendq_size = min(mad_sendq_size, IB_MAD_QP_MAX_SIZE);
	mad_sendq_size = max(mad_sendq_size, IB_MAD_QP_MIN_SIZE);

	INIT_LIST_HEAD(&ib_mad_port_list);

	if (ib_register_client(&mad_client)) {
		pr_err("Couldn't register ib_mad client\n");
		return -EINVAL;
	}

	return 0;
}

void ib_mad_cleanup(void)
{
	ib_unregister_client(&mad_client);
}
