/*
 * Copyright (c) 2007 Cisco Systems, Inc. All rights reserved.
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

#include <rdma/ib_mad.h>
#include <rdma/ib_smi.h>

#include <linux/mlx4/cmd.h>
#include <linux/gfp.h>
#include <rdma/ib_pma.h>

#include "mlx4_ib.h"

enum {
	MLX4_IB_VENDOR_CLASS1 = 0x9,
	MLX4_IB_VENDOR_CLASS2 = 0xa
};

int mlx4_MAD_IFC(struct mlx4_ib_dev *dev, int ignore_mkey, int ignore_bkey,
		 int port, struct ib_wc *in_wc, struct ib_grh *in_grh,
		 void *in_mad, void *response_mad)
{
	struct mlx4_cmd_mailbox *inmailbox, *outmailbox;
	void *inbox;
	int err;
	u32 in_modifier = port;
	u8 op_modifier = 0;

	inmailbox = mlx4_alloc_cmd_mailbox(dev->dev);
	if (IS_ERR(inmailbox))
		return PTR_ERR(inmailbox);
	inbox = inmailbox->buf;

	outmailbox = mlx4_alloc_cmd_mailbox(dev->dev);
	if (IS_ERR(outmailbox)) {
		mlx4_free_cmd_mailbox(dev->dev, inmailbox);
		return PTR_ERR(outmailbox);
	}

	memcpy(inbox, in_mad, 256);

	/*
	 * Key check traps can't be generated unless we have in_wc to
	 * tell us where to send the trap.
	 */
	if (ignore_mkey || !in_wc)
		op_modifier |= 0x1;
	if (ignore_bkey || !in_wc)
		op_modifier |= 0x2;

	if (in_wc) {
		struct {
			__be32		my_qpn;
			u32		reserved1;
			__be32		rqpn;
			u8		sl;
			u8		g_path;
			u16		reserved2[2];
			__be16		pkey;
			u32		reserved3[11];
			u8		grh[40];
		} *ext_info;

		memset(inbox + 256, 0, 256);
		ext_info = inbox + 256;

		ext_info->my_qpn = cpu_to_be32(in_wc->qp->qp_num);
		ext_info->rqpn   = cpu_to_be32(in_wc->src_qp);
		ext_info->sl     = in_wc->sl << 4;
		ext_info->g_path = in_wc->dlid_path_bits |
			(in_wc->wc_flags & IB_WC_GRH ? 0x80 : 0);
		ext_info->pkey   = cpu_to_be16(in_wc->pkey_index);

		if (in_grh)
			memcpy(ext_info->grh, in_grh, 40);

		op_modifier |= 0x4;

		in_modifier |= in_wc->slid << 16;
	}

	err = mlx4_cmd_box(dev->dev, inmailbox->dma, outmailbox->dma,
			   in_modifier, op_modifier,
			   MLX4_CMD_MAD_IFC, MLX4_CMD_TIME_CLASS_C);

	if (!err)
		memcpy(response_mad, outmailbox->buf, 256);

	mlx4_free_cmd_mailbox(dev->dev, inmailbox);
	mlx4_free_cmd_mailbox(dev->dev, outmailbox);

	return err;
}

static void update_sm_ah(struct mlx4_ib_dev *dev, u8 port_num, u16 lid, u8 sl)
{
	struct ib_ah *new_ah;
	struct ib_ah_attr ah_attr;

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

	spin_lock(&dev->sm_lock);
	if (dev->sm_ah[port_num - 1])
		ib_destroy_ah(dev->sm_ah[port_num - 1]);
	dev->sm_ah[port_num - 1] = new_ah;
	spin_unlock(&dev->sm_lock);
}

/*
 * Snoop SM MADs for port info and P_Key table sets, so we can
 * synthesize LID change and P_Key change events.
 */
static void smp_snoop(struct ib_device *ibdev, u8 port_num, struct ib_mad *mad,
				u16 prev_lid)
{
	struct ib_event event;

	if ((mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	     mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
	    mad->mad_hdr.method == IB_MGMT_METHOD_SET) {
		if (mad->mad_hdr.attr_id == IB_SMP_ATTR_PORT_INFO) {
			struct ib_port_info *pinfo =
				(struct ib_port_info *) ((struct ib_smp *) mad)->data;
			u16 lid = be16_to_cpu(pinfo->lid);

			update_sm_ah(to_mdev(ibdev), port_num,
				     be16_to_cpu(pinfo->sm_lid),
				     pinfo->neighbormtu_mastersmsl & 0xf);

			event.device	       = ibdev;
			event.element.port_num = port_num;

			if (pinfo->clientrereg_resv_subnetto & 0x80) {
				event.event    = IB_EVENT_CLIENT_REREGISTER;
				ib_dispatch_event(&event);
			}

			if (prev_lid != lid) {
				event.event    = IB_EVENT_LID_CHANGE;
				ib_dispatch_event(&event);
			}
		}

		if (mad->mad_hdr.attr_id == IB_SMP_ATTR_PKEY_TABLE) {
			event.device	       = ibdev;
			event.event	       = IB_EVENT_PKEY_CHANGE;
			event.element.port_num = port_num;
			ib_dispatch_event(&event);
		}
	}
}

static void node_desc_override(struct ib_device *dev,
			       struct ib_mad *mad)
{
	if ((mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	     mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
	    mad->mad_hdr.method == IB_MGMT_METHOD_GET_RESP &&
	    mad->mad_hdr.attr_id == IB_SMP_ATTR_NODE_DESC) {
		spin_lock(&to_mdev(dev)->sm_lock);
		memcpy(((struct ib_smp *) mad)->data, dev->node_desc, 64);
		spin_unlock(&to_mdev(dev)->sm_lock);
	}
}

static void forward_trap(struct mlx4_ib_dev *dev, u8 port_num, struct ib_mad *mad)
{
	int qpn = mad->mad_hdr.mgmt_class != IB_MGMT_CLASS_SUBN_LID_ROUTED;
	struct ib_mad_send_buf *send_buf;
	struct ib_mad_agent *agent = dev->send_agent[port_num - 1][qpn];
	int ret;

	if (agent) {
		send_buf = ib_create_send_mad(agent, qpn, 0, 0, IB_MGMT_MAD_HDR,
					      IB_MGMT_MAD_DATA, GFP_ATOMIC);
		if (IS_ERR(send_buf))
			return;
		/*
		 * We rely here on the fact that MLX QPs don't use the
		 * address handle after the send is posted (this is
		 * wrong following the IB spec strictly, but we know
		 * it's OK for our devices).
		 */
		spin_lock(&dev->sm_lock);
		memcpy(send_buf->mad, mad, sizeof *mad);
		if ((send_buf->ah = dev->sm_ah[port_num - 1]))
			ret = ib_post_send_mad(send_buf, NULL);
		else
			ret = -EINVAL;
		spin_unlock(&dev->sm_lock);

		if (ret)
			ib_free_send_mad(send_buf);
	}
}

static int ib_process_mad(struct ib_device *ibdev, int mad_flags, u8 port_num,
			struct ib_wc *in_wc, struct ib_grh *in_grh,
			struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	u16 slid, prev_lid = 0;
	int err;
	struct ib_port_attr pattr;

	slid = in_wc ? in_wc->slid : be16_to_cpu(IB_LID_PERMISSIVE);

	if (in_mad->mad_hdr.method == IB_MGMT_METHOD_TRAP && slid == 0) {
		forward_trap(to_mdev(ibdev), port_num, in_mad);
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;
	}

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
		   in_mad->mad_hdr.mgmt_class == MLX4_IB_VENDOR_CLASS1   ||
		   in_mad->mad_hdr.mgmt_class == MLX4_IB_VENDOR_CLASS2   ||
		   in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_CONG_MGMT) {
		if (in_mad->mad_hdr.method  != IB_MGMT_METHOD_GET &&
		    in_mad->mad_hdr.method  != IB_MGMT_METHOD_SET)
			return IB_MAD_RESULT_SUCCESS;
	} else
		return IB_MAD_RESULT_SUCCESS;

	if ((in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	     in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
	    in_mad->mad_hdr.method == IB_MGMT_METHOD_SET &&
	    in_mad->mad_hdr.attr_id == IB_SMP_ATTR_PORT_INFO &&
	    !ib_query_port(ibdev, port_num, &pattr))
		prev_lid = pattr.lid;

	err = mlx4_MAD_IFC(to_mdev(ibdev),
			   mad_flags & IB_MAD_IGNORE_MKEY,
			   mad_flags & IB_MAD_IGNORE_BKEY,
			   port_num, in_wc, in_grh, in_mad, out_mad);
	if (err)
		return IB_MAD_RESULT_FAILURE;

	if (!out_mad->mad_hdr.status) {
		smp_snoop(ibdev, port_num, in_mad, prev_lid);
		node_desc_override(ibdev, out_mad);
	}

	/* set return bit in status of directed route responses */
	if (in_mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
		out_mad->mad_hdr.status |= cpu_to_be16(1 << 15);

	if (in_mad->mad_hdr.method == IB_MGMT_METHOD_TRAP_REPRESS)
		/* no response for trap repress */
		return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED;

	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

static void edit_counter(struct mlx4_counter *cnt,
					struct ib_pma_portcounters *pma_cnt)
{
	pma_cnt->port_xmit_data = cpu_to_be32((be64_to_cpu(cnt->tx_bytes)>>2));
	pma_cnt->port_rcv_data  = cpu_to_be32((be64_to_cpu(cnt->rx_bytes)>>2));
	pma_cnt->port_xmit_packets = cpu_to_be32(be64_to_cpu(cnt->tx_frames));
	pma_cnt->port_rcv_packets  = cpu_to_be32(be64_to_cpu(cnt->rx_frames));
}

static int iboe_process_mad(struct ib_device *ibdev, int mad_flags, u8 port_num,
			struct ib_wc *in_wc, struct ib_grh *in_grh,
			struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	int err;
	u32 inmod = dev->counters[port_num - 1] & 0xffff;
	u8 mode;

	if (in_mad->mad_hdr.mgmt_class != IB_MGMT_CLASS_PERF_MGMT)
		return -EINVAL;

	mailbox = mlx4_alloc_cmd_mailbox(dev->dev);
	if (IS_ERR(mailbox))
		return IB_MAD_RESULT_FAILURE;

	err = mlx4_cmd_box(dev->dev, 0, mailbox->dma, inmod, 0,
			   MLX4_CMD_QUERY_IF_STAT, MLX4_CMD_TIME_CLASS_C);
	if (err)
		err = IB_MAD_RESULT_FAILURE;
	else {
		memset(out_mad->data, 0, sizeof out_mad->data);
		mode = ((struct mlx4_counter *)mailbox->buf)->counter_mode;
		switch (mode & 0xf) {
		case 0:
			edit_counter(mailbox->buf,
						(void *)(out_mad->data + 40));
			err = IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
			break;
		default:
			err = IB_MAD_RESULT_FAILURE;
		}
	}

	mlx4_free_cmd_mailbox(dev->dev, mailbox);

	return err;
}

int mlx4_ib_process_mad(struct ib_device *ibdev, int mad_flags, u8 port_num,
			struct ib_wc *in_wc, struct ib_grh *in_grh,
			struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	switch (rdma_port_get_link_layer(ibdev, port_num)) {
	case IB_LINK_LAYER_INFINIBAND:
		return ib_process_mad(ibdev, mad_flags, port_num, in_wc,
				      in_grh, in_mad, out_mad);
	case IB_LINK_LAYER_ETHERNET:
		return iboe_process_mad(ibdev, mad_flags, port_num, in_wc,
					  in_grh, in_mad, out_mad);
	default:
		return -EINVAL;
	}
}

static void send_handler(struct ib_mad_agent *agent,
			 struct ib_mad_send_wc *mad_send_wc)
{
	ib_free_send_mad(mad_send_wc->send_buf);
}

int mlx4_ib_mad_init(struct mlx4_ib_dev *dev)
{
	struct ib_mad_agent *agent;
	int p, q;
	int ret;
	enum rdma_link_layer ll;

	for (p = 0; p < dev->num_ports; ++p) {
		ll = rdma_port_get_link_layer(&dev->ib_dev, p + 1);
		for (q = 0; q <= 1; ++q) {
			if (ll == IB_LINK_LAYER_INFINIBAND) {
				agent = ib_register_mad_agent(&dev->ib_dev, p + 1,
							      q ? IB_QPT_GSI : IB_QPT_SMI,
							      NULL, 0, send_handler,
							      NULL, NULL);
				if (IS_ERR(agent)) {
					ret = PTR_ERR(agent);
					goto err;
				}
				dev->send_agent[p][q] = agent;
			} else
				dev->send_agent[p][q] = NULL;
		}
	}

	return 0;

err:
	for (p = 0; p < dev->num_ports; ++p)
		for (q = 0; q <= 1; ++q)
			if (dev->send_agent[p][q])
				ib_unregister_mad_agent(dev->send_agent[p][q]);

	return ret;
}

void mlx4_ib_mad_cleanup(struct mlx4_ib_dev *dev)
{
	struct ib_mad_agent *agent;
	int p, q;

	for (p = 0; p < dev->num_ports; ++p) {
		for (q = 0; q <= 1; ++q) {
			agent = dev->send_agent[p][q];
			if (agent) {
				dev->send_agent[p][q] = NULL;
				ib_unregister_mad_agent(agent);
			}
		}

		if (dev->sm_ah[p])
			ib_destroy_ah(dev->sm_ah[p]);
	}
}
