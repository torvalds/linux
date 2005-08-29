/*
 * Copyright (c) 2004, 2005 Mellanox Technologies Ltd.  All rights reserved.
 * Copyright (c) 2004, 2005 Infinicon Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Intel Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2004, 2005 Voltaire Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
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
 * $Id: agent.c 1389 2004-12-27 22:56:47Z roland $
 */

#include <linux/dma-mapping.h>

#include <asm/bug.h>

#include <rdma/ib_smi.h>

#include "smi.h"
#include "agent_priv.h"
#include "mad_priv.h"
#include "agent.h"

spinlock_t ib_agent_port_list_lock;
static LIST_HEAD(ib_agent_port_list);

/*
 * Caller must hold ib_agent_port_list_lock
 */
static inline struct ib_agent_port_private *
__ib_get_agent_port(struct ib_device *device, int port_num,
		    struct ib_mad_agent *mad_agent)
{
	struct ib_agent_port_private *entry;

	BUG_ON(!(!!device ^ !!mad_agent));  /* Exactly one MUST be (!NULL) */

	if (device) {
		list_for_each_entry(entry, &ib_agent_port_list, port_list) {
			if (entry->smp_agent->device == device &&
			    entry->port_num == port_num)
				return entry;
		}
	} else {
		list_for_each_entry(entry, &ib_agent_port_list, port_list) {
			if ((entry->smp_agent == mad_agent) ||
			    (entry->perf_mgmt_agent == mad_agent))
				return entry;
		}
	}
	return NULL;
}

static inline struct ib_agent_port_private *
ib_get_agent_port(struct ib_device *device, int port_num,
		  struct ib_mad_agent *mad_agent)
{
	struct ib_agent_port_private *entry;
	unsigned long flags;

	spin_lock_irqsave(&ib_agent_port_list_lock, flags);
	entry = __ib_get_agent_port(device, port_num, mad_agent);
	spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);

	return entry;
}

int smi_check_local_dr_smp(struct ib_smp *smp,
			   struct ib_device *device,
			   int port_num)
{
	struct ib_agent_port_private *port_priv;

	if (smp->mgmt_class != IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		return 1;
	port_priv = ib_get_agent_port(device, port_num, NULL);
	if (!port_priv) {
		printk(KERN_DEBUG SPFX "smi_check_local_dr_smp %s port %d "
		       "not open\n",
		       device->name, port_num);
		return 1;
	}

	return smi_check_local_smp(port_priv->smp_agent, smp);
}

static int agent_mad_send(struct ib_mad_agent *mad_agent,
			  struct ib_agent_port_private *port_priv,
			  struct ib_mad_private *mad_priv,
			  struct ib_grh *grh,
			  struct ib_wc *wc)
{
	struct ib_agent_send_wr *agent_send_wr;
	struct ib_sge gather_list;
	struct ib_send_wr send_wr;
	struct ib_send_wr *bad_send_wr;
	struct ib_ah_attr ah_attr;
	unsigned long flags;
	int ret = 1;

	agent_send_wr = kmalloc(sizeof(*agent_send_wr), GFP_KERNEL);
	if (!agent_send_wr)
		goto out;
	agent_send_wr->mad = mad_priv;

	gather_list.addr = dma_map_single(mad_agent->device->dma_device,
					  &mad_priv->mad,
					  sizeof(mad_priv->mad),
					  DMA_TO_DEVICE);
	gather_list.length = sizeof(mad_priv->mad);
	gather_list.lkey = mad_agent->mr->lkey;

	send_wr.next = NULL;
	send_wr.opcode = IB_WR_SEND;
	send_wr.sg_list = &gather_list;
	send_wr.num_sge = 1;
	send_wr.wr.ud.remote_qpn = wc->src_qp; /* DQPN */
	send_wr.wr.ud.timeout_ms = 0;
	send_wr.send_flags = IB_SEND_SIGNALED | IB_SEND_SOLICITED;

	ah_attr.dlid = wc->slid;
	ah_attr.port_num = mad_agent->port_num;
	ah_attr.src_path_bits = wc->dlid_path_bits;
	ah_attr.sl = wc->sl;
	ah_attr.static_rate = 0;
	ah_attr.ah_flags = 0; /* No GRH */
	if (mad_priv->mad.mad.mad_hdr.mgmt_class == IB_MGMT_CLASS_PERF_MGMT) {
		if (wc->wc_flags & IB_WC_GRH) {
			ah_attr.ah_flags = IB_AH_GRH;
			/* Should sgid be looked up ? */
			ah_attr.grh.sgid_index = 0;
			ah_attr.grh.hop_limit = grh->hop_limit;
			ah_attr.grh.flow_label = be32_to_cpu(
				grh->version_tclass_flow)  & 0xfffff;
			ah_attr.grh.traffic_class = (be32_to_cpu(
				grh->version_tclass_flow) >> 20) & 0xff;
			memcpy(ah_attr.grh.dgid.raw,
			       grh->sgid.raw,
			       sizeof(ah_attr.grh.dgid));
		}
	}

	agent_send_wr->ah = ib_create_ah(mad_agent->qp->pd, &ah_attr);
	if (IS_ERR(agent_send_wr->ah)) {
		printk(KERN_ERR SPFX "No memory for address handle\n");
		kfree(agent_send_wr);
		goto out;
	}

	send_wr.wr.ud.ah = agent_send_wr->ah;
	if (mad_priv->mad.mad.mad_hdr.mgmt_class == IB_MGMT_CLASS_PERF_MGMT) {
		send_wr.wr.ud.pkey_index = wc->pkey_index;
		send_wr.wr.ud.remote_qkey = IB_QP1_QKEY;
	} else { 	/* for SMPs */
		send_wr.wr.ud.pkey_index = 0;
		send_wr.wr.ud.remote_qkey = 0;
	}
	send_wr.wr.ud.mad_hdr = &mad_priv->mad.mad.mad_hdr;
	send_wr.wr_id = (unsigned long)agent_send_wr;

	pci_unmap_addr_set(agent_send_wr, mapping, gather_list.addr);

	/* Send */
	spin_lock_irqsave(&port_priv->send_list_lock, flags);
	if (ib_post_send_mad(mad_agent, &send_wr, &bad_send_wr)) {
		spin_unlock_irqrestore(&port_priv->send_list_lock, flags);
		dma_unmap_single(mad_agent->device->dma_device,
				 pci_unmap_addr(agent_send_wr, mapping),
				 sizeof(mad_priv->mad),
				 DMA_TO_DEVICE);
		ib_destroy_ah(agent_send_wr->ah);
		kfree(agent_send_wr);
	} else {
		list_add_tail(&agent_send_wr->send_list,
			      &port_priv->send_posted_list);
		spin_unlock_irqrestore(&port_priv->send_list_lock, flags);
		ret = 0;
	}

out:
	return ret;
}

int agent_send(struct ib_mad_private *mad,
	       struct ib_grh *grh,
	       struct ib_wc *wc,
	       struct ib_device *device,
	       int port_num)
{
	struct ib_agent_port_private *port_priv;
	struct ib_mad_agent *mad_agent;

	port_priv = ib_get_agent_port(device, port_num, NULL);
	if (!port_priv) {
		printk(KERN_DEBUG SPFX "agent_send %s port %d not open\n",
		       device->name, port_num);
		return 1;
	}

	/* Get mad agent based on mgmt_class in MAD */
	switch (mad->mad.mad.mad_hdr.mgmt_class) {
		case IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE:
		case IB_MGMT_CLASS_SUBN_LID_ROUTED:
			mad_agent = port_priv->smp_agent;
			break;
		case IB_MGMT_CLASS_PERF_MGMT:
			mad_agent = port_priv->perf_mgmt_agent;
			break;
		default:
			return 1;
	}

	return agent_mad_send(mad_agent, port_priv, mad, grh, wc);
}

static void agent_send_handler(struct ib_mad_agent *mad_agent,
			       struct ib_mad_send_wc *mad_send_wc)
{
	struct ib_agent_port_private	*port_priv;
	struct ib_agent_send_wr		*agent_send_wr;
	unsigned long			flags;

	/* Find matching MAD agent */
	port_priv = ib_get_agent_port(NULL, 0, mad_agent);
	if (!port_priv) {
		printk(KERN_ERR SPFX "agent_send_handler: no matching MAD "
		       "agent %p\n", mad_agent);
		return;
	}

	agent_send_wr = (struct ib_agent_send_wr *)(unsigned long)mad_send_wc->wr_id;
	spin_lock_irqsave(&port_priv->send_list_lock, flags);
	/* Remove completed send from posted send MAD list */
	list_del(&agent_send_wr->send_list);
	spin_unlock_irqrestore(&port_priv->send_list_lock, flags);

	dma_unmap_single(mad_agent->device->dma_device,
			 pci_unmap_addr(agent_send_wr, mapping),
			 sizeof(agent_send_wr->mad->mad),
			 DMA_TO_DEVICE);

	ib_destroy_ah(agent_send_wr->ah);

	/* Release allocated memory */
	kmem_cache_free(ib_mad_cache, agent_send_wr->mad);
	kfree(agent_send_wr);
}

int ib_agent_port_open(struct ib_device *device, int port_num)
{
	int ret;
	struct ib_agent_port_private *port_priv;
	unsigned long flags;

	/* First, check if port already open for SMI */
	port_priv = ib_get_agent_port(device, port_num, NULL);
	if (port_priv) {
		printk(KERN_DEBUG SPFX "%s port %d already open\n",
		       device->name, port_num);
		return 0;
	}

	/* Create new device info */
	port_priv = kmalloc(sizeof *port_priv, GFP_KERNEL);
	if (!port_priv) {
		printk(KERN_ERR SPFX "No memory for ib_agent_port_private\n");
		ret = -ENOMEM;
		goto error1;
	}

	memset(port_priv, 0, sizeof *port_priv);
	port_priv->port_num = port_num;
	spin_lock_init(&port_priv->send_list_lock);
	INIT_LIST_HEAD(&port_priv->send_posted_list);

	/* Obtain send only MAD agent for SM class (SMI QP) */
	port_priv->smp_agent = ib_register_mad_agent(device, port_num,
						     IB_QPT_SMI,
						     NULL, 0,
						    &agent_send_handler,
						     NULL, NULL);

	if (IS_ERR(port_priv->smp_agent)) {
		ret = PTR_ERR(port_priv->smp_agent);
		goto error2;
	}

	/* Obtain send only MAD agent for PerfMgmt class (GSI QP) */
	port_priv->perf_mgmt_agent = ib_register_mad_agent(device, port_num,
							   IB_QPT_GSI,
							   NULL, 0,
							  &agent_send_handler,
							   NULL, NULL);
	if (IS_ERR(port_priv->perf_mgmt_agent)) {
		ret = PTR_ERR(port_priv->perf_mgmt_agent);
		goto error3;
	}

	spin_lock_irqsave(&ib_agent_port_list_lock, flags);
	list_add_tail(&port_priv->port_list, &ib_agent_port_list);
	spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);

	return 0;

error3:
	ib_unregister_mad_agent(port_priv->smp_agent);
error2:
	kfree(port_priv);
error1:
	return ret;
}

int ib_agent_port_close(struct ib_device *device, int port_num)
{
	struct ib_agent_port_private *port_priv;
	unsigned long flags;

	spin_lock_irqsave(&ib_agent_port_list_lock, flags);
	port_priv = __ib_get_agent_port(device, port_num, NULL);
	if (port_priv == NULL) {
		spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);
		printk(KERN_ERR SPFX "Port %d not found\n", port_num);
		return -ENODEV;
	}
	list_del(&port_priv->port_list);
	spin_unlock_irqrestore(&ib_agent_port_list_lock, flags);

	ib_unregister_mad_agent(port_priv->perf_mgmt_agent);
	ib_unregister_mad_agent(port_priv->smp_agent);
	kfree(port_priv);

	return 0;
}
