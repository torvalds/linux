/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2004 Voltaire, Inc. All rights reserved.
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
 * $Id: mthca_mad.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <rdma/ib_verbs.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_smi.h>

#include "mthca_dev.h"
#include "mthca_cmd.h"

enum {
	MTHCA_VENDOR_CLASS1 = 0x9,
	MTHCA_VENDOR_CLASS2 = 0xa
};

struct mthca_trap_mad {
	struct ib_mad *mad;
	DECLARE_PCI_UNMAP_ADDR(mapping)
};

static void update_sm_ah(struct mthca_dev *dev,
			 u8 port_num, u16 lid, u8 sl)
{
	struct ib_ah *new_ah;
	struct ib_ah_attr ah_attr;
	unsigned long flags;

	if (!dev->send_agent[port_num - 1][0])
		return;

	memset(&ah_attr, 0, sizeof ah_attr);
	ah_attr.dlid     = lid;
	ah_attr.sl       = sl;
	ah_attr.port_num = port_num;

	new_ah = ib_create_ah(dev->send_agent[port_num - 1][0]->qp->pd,
			      &ah_attr);
	if (IS_ERR(new_ah))
		return;

	spin_lock_irqsave(&dev->sm_lock, flags);
	if (dev->sm_ah[port_num - 1])
		ib_destroy_ah(dev->sm_ah[port_num - 1]);
	dev->sm_ah[port_num - 1] = new_ah;
	spin_unlock_irqrestore(&dev->sm_lock, flags);
}

/*
 * Snoop SM MADs for port info and P_Key table sets, so we can
 * synthesize LID change and P_Key change events.
 */
static void smp_snoop(struct ib_device *ibdev,
		      u8 port_num,
		      struct ib_mad *mad)
{
	struct ib_event event;

	if ((mad->mad_hdr.mgmt_class  == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	     mad->mad_hdr.mgmt_class  == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
	    mad->mad_hdr.method     == IB_MGMT_METHOD_SET) {
		if (mad->mad_hdr.attr_id == IB_SMP_ATTR_PORT_INFO) {
			update_sm_ah(to_mdev(ibdev), port_num,
				     be16_to_cpup((__be16 *) (mad->data + 58)),
				     (*(u8 *) (mad->data + 76)) & 0xf);

			event.device           = ibdev;
			event.event            = IB_EVENT_LID_CHANGE;
			event.element.port_num = port_num;
			ib_dispatch_event(&event);
		}

		if (mad->mad_hdr.attr_id == IB_SMP_ATTR_PKEY_TABLE) {
			event.device           = ibdev;
			event.event            = IB_EVENT_PKEY_CHANGE;
			event.element.port_num = port_num;
			ib_dispatch_event(&event);
		}
	}
}

static void forward_trap(struct mthca_dev *dev,
			 u8 port_num,
			 struct ib_mad *mad)
{
	int qpn = mad->mad_hdr.mgmt_class != IB_MGMT_CLASS_SUBN_LID_ROUTED;
	struct mthca_trap_mad *tmad;
	struct ib_sge      gather_list;
	struct ib_send_wr *bad_wr, wr = {
		.opcode      = IB_WR_SEND,
		.sg_list     = &gather_list,
		.num_sge     = 1,
		.send_flags  = IB_SEND_SIGNALED,
		.wr	     = {
			 .ud = {
				 .remote_qpn  = qpn,
				 .remote_qkey = qpn ? IB_QP1_QKEY : 0,
				 .timeout_ms  = 0
			 }
		 }
	};
	struct ib_mad_agent *agent = dev->send_agent[port_num - 1][qpn];
	int ret;
	unsigned long flags;

	if (agent) {
		tmad = kmalloc(sizeof *tmad, GFP_KERNEL);
		if (!tmad)
			return;

		tmad->mad = kmalloc(sizeof *tmad->mad, GFP_KERNEL);
		if (!tmad->mad) {
			kfree(tmad);
			return;
		}

		memcpy(tmad->mad, mad, sizeof *mad);

		wr.wr.ud.mad_hdr = &tmad->mad->mad_hdr;
		wr.wr_id         = (unsigned long) tmad;

		gather_list.addr   = dma_map_single(agent->device->dma_device,
						    tmad->mad,
						    sizeof *tmad->mad,
						    DMA_TO_DEVICE);
		gather_list.length = sizeof *tmad->mad;
		gather_list.lkey   = to_mpd(agent->qp->pd)->ntmr.ibmr.lkey;
		pci_unmap_addr_set(tmad, mapping, gather_list.addr);

		/*
		 * We rely here on the fact that MLX QPs don't use the
		 * address handle after the send is posted (this is
		 * wrong following the IB spec strictly, but we know
		 * it's OK for our devices).
		 */
		spin_lock_irqsave(&dev->sm_lock, flags);
		wr.wr.ud.ah      = dev->sm_ah[port_num - 1];
		if (wr.wr.ud.ah)
			ret = ib_post_send_mad(agent, &wr, &bad_wr);
		else
			ret = -EINVAL;
		spin_unlock_irqrestore(&dev->sm_lock, flags);

		if (ret) {
			dma_unmap_single(agent->device->dma_device,
					 pci_unmap_addr(tmad, mapping),
					 sizeof *tmad->mad,
					 DMA_TO_DEVICE);
			kfree(tmad->mad);
			kfree(tmad);
		}
	}
}

int mthca_process_mad(struct ib_device *ibdev,
		      int mad_flags,
		      u8 port_num,
		      struct ib_wc *in_wc,
		      struct ib_grh *in_grh,
		      struct ib_mad *in_mad,
		      struct ib_mad *out_mad)
{
	int err;
	u8 status;
	u16 slid = in_wc ? in_wc->slid : be16_to_cpu(IB_LID_PERMISSIVE);

	/* Forward locally generated traps to the SM */
	if (in_mad->mad_hdr.method == IB_MGMT_METHOD_TRAP &&
	    slid == 0) {
		forward_trap(to_mdev(ibdev), port_num, in_mad);
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
	}

	/*
	 * Only handle SM gets, sets and trap represses for SM class
	 *
	 * Only handle PMA and Mellanox vendor-specific class gets and
	 * sets for other classes.
	 */
	if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	    in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) {
		if (in_mad->mad_hdr.method   != IB_MGMT_METHOD_GET &&
		    in_mad->mad_hdr.method   != IB_MGMT_METHOD_SET &&
		    in_mad->mad_hdr.method   != IB_MGMT_METHOD_TRAP_REPRESS)
			return IB_MAD_RESULT_SUCCESS;

		/*
		 * Don't process SMInfo queries or vendor-specific
		 * MADs -- the SMA can't handle them.
		 */
		if (in_mad->mad_hdr.attr_id == IB_SMP_ATTR_SM_INFO ||
		    ((in_mad->mad_hdr.attr_id & IB_SMP_ATTR_VENDOR_MASK) ==
		     IB_SMP_ATTR_VENDOR_MASK))
			return IB_MAD_RESULT_SUCCESS;
	} else if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_PERF_MGMT ||
		   in_mad->mad_hdr.mgmt_class == MTHCA_VENDOR_CLASS1     ||
		   in_mad->mad_hdr.mgmt_class == MTHCA_VENDOR_CLASS2) {
		if (in_mad->mad_hdr.method  != IB_MGMT_METHOD_GET &&
		    in_mad->mad_hdr.method  != IB_MGMT_METHOD_SET)
			return IB_MAD_RESULT_SUCCESS;
	} else
		return IB_MAD_RESULT_SUCCESS;

	err = mthca_MAD_IFC(to_mdev(ibdev),
			    mad_flags & IB_MAD_IGNORE_MKEY,
			    mad_flags & IB_MAD_IGNORE_BKEY,
			    port_num, in_wc, in_grh, in_mad, out_mad,
			    &status);
	if (err) {
		mthca_err(to_mdev(ibdev), "MAD_IFC failed\n");
		return IB_MAD_RESULT_FAILURE;
	}
	if (status == MTHCA_CMD_STAT_BAD_PKT)
		return IB_MAD_RESULT_SUCCESS;
	if (status) {
		mthca_err(to_mdev(ibdev), "MAD_IFC returned status %02x\n",
			  status);
		return IB_MAD_RESULT_FAILURE;
	}

	if (!out_mad->mad_hdr.status)
		smp_snoop(ibdev, port_num, in_mad);

	/* set return bit in status of directed route responses */
	if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		out_mad->mad_hdr.status |= cpu_to_be16(1 << 15);

	if (in_mad->mad_hdr.method == IB_MGMT_METHOD_TRAP_REPRESS)
		/* no response for trap repress */
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;

	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

static void send_handler(struct ib_mad_agent *agent,
			 struct ib_mad_send_wc *mad_send_wc)
{
	struct mthca_trap_mad *tmad =
		(void *) (unsigned long) mad_send_wc->wr_id;

	dma_unmap_single(agent->device->dma_device,
			 pci_unmap_addr(tmad, mapping),
			 sizeof *tmad->mad,
			 DMA_TO_DEVICE);
	kfree(tmad->mad);
	kfree(tmad);
}

int mthca_create_agents(struct mthca_dev *dev)
{
	struct ib_mad_agent *agent;
	int p, q;

	spin_lock_init(&dev->sm_lock);

	for (p = 0; p < dev->limits.num_ports; ++p)
		for (q = 0; q <= 1; ++q) {
			agent = ib_register_mad_agent(&dev->ib_dev, p + 1,
						      q ? IB_QPT_GSI : IB_QPT_SMI,
						      NULL, 0, send_handler,
						      NULL, NULL);
			if (IS_ERR(agent))
				goto err;
			dev->send_agent[p][q] = agent;
		}

	return 0;

err:
	for (p = 0; p < dev->limits.num_ports; ++p)
		for (q = 0; q <= 1; ++q)
			if (dev->send_agent[p][q])
				ib_unregister_mad_agent(dev->send_agent[p][q]);

	return PTR_ERR(agent);
}

void mthca_free_agents(struct mthca_dev *dev)
{
	struct ib_mad_agent *agent;
	int p, q;

	for (p = 0; p < dev->limits.num_ports; ++p) {
		for (q = 0; q <= 1; ++q) {
			agent = dev->send_agent[p][q];
			dev->send_agent[p][q] = NULL;
			ib_unregister_mad_agent(agent);
		}

		if (dev->sm_ah[p])
			ib_destroy_ah(dev->sm_ah[p]);
	}
}
