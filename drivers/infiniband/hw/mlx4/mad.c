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

#define MLX4_TUN_SEND_WRID_SHIFT 34
#define MLX4_TUN_QPN_SHIFT 32
#define MLX4_TUN_WRID_RECV (((u64) 1) << MLX4_TUN_SEND_WRID_SHIFT)
#define MLX4_TUN_SET_WRID_QPN(a) (((u64) ((a) & 0x3)) << MLX4_TUN_QPN_SHIFT)

#define MLX4_TUN_IS_RECV(a)  (((a) >>  MLX4_TUN_SEND_WRID_SHIFT) & 0x1)
#define MLX4_TUN_WRID_QPN(a) (((a) >> MLX4_TUN_QPN_SHIFT) & 0x3)

struct mlx4_mad_rcv_buf {
	struct ib_grh grh;
	u8 payload[256];
} __packed;

struct mlx4_mad_snd_buf {
	u8 payload[256];
} __packed;

struct mlx4_tunnel_mad {
	struct ib_grh grh;
	struct mlx4_ib_tunnel_header hdr;
	struct ib_mad mad;
} __packed;

struct mlx4_rcv_tunnel_mad {
	struct mlx4_rcv_tunnel_hdr hdr;
	struct ib_grh grh;
	struct ib_mad mad;
} __packed;

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
			   MLX4_CMD_MAD_IFC, MLX4_CMD_TIME_CLASS_C,
			   MLX4_CMD_NATIVE);

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
 * Snoop SM MADs for port info, GUID info, and  P_Key table sets, so we can
 * synthesize LID change, Client-Rereg, GID change, and P_Key change events.
 */
static void smp_snoop(struct ib_device *ibdev, u8 port_num, struct ib_mad *mad,
		      u16 prev_lid)
{
	struct ib_port_info *pinfo;
	u16 lid;
	__be16 *base;
	u32 bn, pkey_change_bitmap;
	int i;


	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	if ((mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	     mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
	    mad->mad_hdr.method == IB_MGMT_METHOD_SET)
		switch (mad->mad_hdr.attr_id) {
		case IB_SMP_ATTR_PORT_INFO:
			pinfo = (struct ib_port_info *) ((struct ib_smp *) mad)->data;
			lid = be16_to_cpu(pinfo->lid);

			update_sm_ah(dev, port_num,
				     be16_to_cpu(pinfo->sm_lid),
				     pinfo->neighbormtu_mastersmsl & 0xf);

			if (pinfo->clientrereg_resv_subnetto & 0x80)
				mlx4_ib_dispatch_event(dev, port_num,
						       IB_EVENT_CLIENT_REREGISTER);

			if (prev_lid != lid)
				mlx4_ib_dispatch_event(dev, port_num,
						       IB_EVENT_LID_CHANGE);
			break;

		case IB_SMP_ATTR_PKEY_TABLE:
			if (!mlx4_is_mfunc(dev->dev)) {
				mlx4_ib_dispatch_event(dev, port_num,
						       IB_EVENT_PKEY_CHANGE);
				break;
			}

			bn  = be32_to_cpu(((struct ib_smp *)mad)->attr_mod) & 0xFFFF;
			base = (__be16 *) &(((struct ib_smp *)mad)->data[0]);
			pkey_change_bitmap = 0;
			for (i = 0; i < 32; i++) {
				pr_debug("PKEY[%d] = x%x\n",
					 i + bn*32, be16_to_cpu(base[i]));
				if (be16_to_cpu(base[i]) !=
				    dev->pkeys.phys_pkey_cache[port_num - 1][i + bn*32]) {
					pkey_change_bitmap |= (1 << i);
					dev->pkeys.phys_pkey_cache[port_num - 1][i + bn*32] =
						be16_to_cpu(base[i]);
				}
			}
			pr_debug("PKEY Change event: port=%d, "
				 "block=0x%x, change_bitmap=0x%x\n",
				 port_num, bn, pkey_change_bitmap);

			if (pkey_change_bitmap)
				mlx4_ib_dispatch_event(dev, port_num,
						       IB_EVENT_PKEY_CHANGE);

			break;

		case IB_SMP_ATTR_GUID_INFO:
			/* paravirtualized master's guid is guid 0 -- does not change */
			if (!mlx4_is_master(dev->dev))
				mlx4_ib_dispatch_event(dev, port_num,
						       IB_EVENT_GID_CHANGE);
			break;
		default:
			break;
		}
}

static void node_desc_override(struct ib_device *dev,
			       struct ib_mad *mad)
{
	unsigned long flags;

	if ((mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED ||
	     mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) &&
	    mad->mad_hdr.method == IB_MGMT_METHOD_GET_RESP &&
	    mad->mad_hdr.attr_id == IB_SMP_ATTR_NODE_DESC) {
		spin_lock_irqsave(&to_mdev(dev)->sm_lock, flags);
		memcpy(((struct ib_smp *) mad)->data, dev->node_desc, 64);
		spin_unlock_irqrestore(&to_mdev(dev)->sm_lock, flags);
	}
}

static void forward_trap(struct mlx4_ib_dev *dev, u8 port_num, struct ib_mad *mad)
{
	int qpn = mad->mad_hdr.mgmt_class != IB_MGMT_CLASS_SUBN_LID_ROUTED;
	struct ib_mad_send_buf *send_buf;
	struct ib_mad_agent *agent = dev->send_agent[port_num - 1][qpn];
	int ret;
	unsigned long flags;

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
		spin_lock_irqsave(&dev->sm_lock, flags);
		memcpy(send_buf->mad, mad, sizeof *mad);
		if ((send_buf->ah = dev->sm_ah[port_num - 1]))
			ret = ib_post_send_mad(send_buf, NULL);
		else
			ret = -EINVAL;
		spin_unlock_irqrestore(&dev->sm_lock, flags);

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

	if (in_wc && in_wc->qp->qp_num) {
		pr_debug("received MAD: slid:%d sqpn:%d "
			"dlid_bits:%d dqpn:%d wc_flags:0x%x, cls %x, mtd %x, atr %x\n",
			in_wc->slid, in_wc->src_qp,
			in_wc->dlid_path_bits,
			in_wc->qp->qp_num,
			in_wc->wc_flags,
			in_mad->mad_hdr.mgmt_class, in_mad->mad_hdr.method,
			be16_to_cpu(in_mad->mad_hdr.attr_id));
		if (in_wc->wc_flags & IB_WC_GRH) {
			pr_debug("sgid_hi:0x%016llx sgid_lo:0x%016llx\n",
				 be64_to_cpu(in_grh->sgid.global.subnet_prefix),
				 be64_to_cpu(in_grh->sgid.global.interface_id));
			pr_debug("dgid_hi:0x%016llx dgid_lo:0x%016llx\n",
				 be64_to_cpu(in_grh->dgid.global.subnet_prefix),
				 be64_to_cpu(in_grh->dgid.global.interface_id));
		}
	}

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
		 * Don't process SMInfo queries -- the SMA can't handle them.
		 */
		if (in_mad->mad_hdr.attr_id == IB_SMP_ATTR_SM_INFO)
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
		if (!(to_mdev(ibdev)->dev->caps.flags & MLX4_DEV_CAP_FLAG_PORT_MNG_CHG_EV))
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
			   MLX4_CMD_QUERY_IF_STAT, MLX4_CMD_TIME_CLASS_C,
			   MLX4_CMD_WRAPPED);
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

void handle_port_mgmt_change_event(struct work_struct *work)
{
	struct ib_event_work *ew = container_of(work, struct ib_event_work, work);
	struct mlx4_ib_dev *dev = ew->ib_dev;
	struct mlx4_eqe *eqe = &(ew->ib_eqe);
	u8 port = eqe->event.port_mgmt_change.port;
	u32 changed_attr;

	switch (eqe->subtype) {
	case MLX4_DEV_PMC_SUBTYPE_PORT_INFO:
		changed_attr = be32_to_cpu(eqe->event.port_mgmt_change.params.port_info.changed_attr);

		/* Update the SM ah - This should be done before handling
		   the other changed attributes so that MADs can be sent to the SM */
		if (changed_attr & MSTR_SM_CHANGE_MASK) {
			u16 lid = be16_to_cpu(eqe->event.port_mgmt_change.params.port_info.mstr_sm_lid);
			u8 sl = eqe->event.port_mgmt_change.params.port_info.mstr_sm_sl & 0xf;
			update_sm_ah(dev, port, lid, sl);
		}

		/* Check if it is a lid change event */
		if (changed_attr & MLX4_EQ_PORT_INFO_LID_CHANGE_MASK)
			mlx4_ib_dispatch_event(dev, port, IB_EVENT_LID_CHANGE);

		/* Generate GUID changed event */
		if (changed_attr & MLX4_EQ_PORT_INFO_GID_PFX_CHANGE_MASK)
			mlx4_ib_dispatch_event(dev, port, IB_EVENT_GID_CHANGE);

		if (changed_attr & MLX4_EQ_PORT_INFO_CLIENT_REREG_MASK)
			mlx4_ib_dispatch_event(dev, port,
					       IB_EVENT_CLIENT_REREGISTER);
		break;

	case MLX4_DEV_PMC_SUBTYPE_PKEY_TABLE:
		mlx4_ib_dispatch_event(dev, port, IB_EVENT_PKEY_CHANGE);
		break;
	case MLX4_DEV_PMC_SUBTYPE_GUID_INFO:
		/* paravirtualized master's guid is guid 0 -- does not change */
		if (!mlx4_is_master(dev->dev))
			mlx4_ib_dispatch_event(dev, port, IB_EVENT_GID_CHANGE);
		break;
	default:
		pr_warn("Unsupported subtype 0x%x for "
			"Port Management Change event\n", eqe->subtype);
	}

	kfree(ew);
}

void mlx4_ib_dispatch_event(struct mlx4_ib_dev *dev, u8 port_num,
			    enum ib_event_type type)
{
	struct ib_event event;

	event.device		= &dev->ib_dev;
	event.element.port_num	= port_num;
	event.event		= type;

	ib_dispatch_event(&event);
}

static void mlx4_ib_tunnel_comp_handler(struct ib_cq *cq, void *arg)
{
	unsigned long flags;
	struct mlx4_ib_demux_pv_ctx *ctx = cq->cq_context;
	struct mlx4_ib_dev *dev = to_mdev(ctx->ib_dev);
	spin_lock_irqsave(&dev->sriov.going_down_lock, flags);
	if (!dev->sriov.is_going_down && ctx->state == DEMUX_PV_STATE_ACTIVE)
		queue_work(ctx->wq, &ctx->work);
	spin_unlock_irqrestore(&dev->sriov.going_down_lock, flags);
}

static int mlx4_ib_post_pv_qp_buf(struct mlx4_ib_demux_pv_ctx *ctx,
				  struct mlx4_ib_demux_pv_qp *tun_qp,
				  int index)
{
	struct ib_sge sg_list;
	struct ib_recv_wr recv_wr, *bad_recv_wr;
	int size;

	size = (tun_qp->qp->qp_type == IB_QPT_UD) ?
		sizeof (struct mlx4_tunnel_mad) : sizeof (struct mlx4_mad_rcv_buf);

	sg_list.addr = tun_qp->ring[index].map;
	sg_list.length = size;
	sg_list.lkey = ctx->mr->lkey;

	recv_wr.next = NULL;
	recv_wr.sg_list = &sg_list;
	recv_wr.num_sge = 1;
	recv_wr.wr_id = (u64) index | MLX4_TUN_WRID_RECV |
		MLX4_TUN_SET_WRID_QPN(tun_qp->proxy_qpt);
	ib_dma_sync_single_for_device(ctx->ib_dev, tun_qp->ring[index].map,
				      size, DMA_FROM_DEVICE);
	return ib_post_recv(tun_qp->qp, &recv_wr, &bad_recv_wr);
}

static int mlx4_ib_alloc_pv_bufs(struct mlx4_ib_demux_pv_ctx *ctx,
				 enum ib_qp_type qp_type, int is_tun)
{
	int i;
	struct mlx4_ib_demux_pv_qp *tun_qp;
	int rx_buf_size, tx_buf_size;

	if (qp_type > IB_QPT_GSI)
		return -EINVAL;

	tun_qp = &ctx->qp[qp_type];

	tun_qp->ring = kzalloc(sizeof (struct mlx4_ib_buf) * MLX4_NUM_TUNNEL_BUFS,
			       GFP_KERNEL);
	if (!tun_qp->ring)
		return -ENOMEM;

	tun_qp->tx_ring = kcalloc(MLX4_NUM_TUNNEL_BUFS,
				  sizeof (struct mlx4_ib_tun_tx_buf),
				  GFP_KERNEL);
	if (!tun_qp->tx_ring) {
		kfree(tun_qp->ring);
		tun_qp->ring = NULL;
		return -ENOMEM;
	}

	if (is_tun) {
		rx_buf_size = sizeof (struct mlx4_tunnel_mad);
		tx_buf_size = sizeof (struct mlx4_rcv_tunnel_mad);
	} else {
		rx_buf_size = sizeof (struct mlx4_mad_rcv_buf);
		tx_buf_size = sizeof (struct mlx4_mad_snd_buf);
	}

	for (i = 0; i < MLX4_NUM_TUNNEL_BUFS; i++) {
		tun_qp->ring[i].addr = kmalloc(rx_buf_size, GFP_KERNEL);
		if (!tun_qp->ring[i].addr)
			goto err;
		tun_qp->ring[i].map = ib_dma_map_single(ctx->ib_dev,
							tun_qp->ring[i].addr,
							rx_buf_size,
							DMA_FROM_DEVICE);
	}

	for (i = 0; i < MLX4_NUM_TUNNEL_BUFS; i++) {
		tun_qp->tx_ring[i].buf.addr =
			kmalloc(tx_buf_size, GFP_KERNEL);
		if (!tun_qp->tx_ring[i].buf.addr)
			goto tx_err;
		tun_qp->tx_ring[i].buf.map =
			ib_dma_map_single(ctx->ib_dev,
					  tun_qp->tx_ring[i].buf.addr,
					  tx_buf_size,
					  DMA_TO_DEVICE);
		tun_qp->tx_ring[i].ah = NULL;
	}
	spin_lock_init(&tun_qp->tx_lock);
	tun_qp->tx_ix_head = 0;
	tun_qp->tx_ix_tail = 0;
	tun_qp->proxy_qpt = qp_type;

	return 0;

tx_err:
	while (i > 0) {
		--i;
		ib_dma_unmap_single(ctx->ib_dev, tun_qp->tx_ring[i].buf.map,
				    tx_buf_size, DMA_TO_DEVICE);
		kfree(tun_qp->tx_ring[i].buf.addr);
	}
	kfree(tun_qp->tx_ring);
	tun_qp->tx_ring = NULL;
	i = MLX4_NUM_TUNNEL_BUFS;
err:
	while (i > 0) {
		--i;
		ib_dma_unmap_single(ctx->ib_dev, tun_qp->ring[i].map,
				    rx_buf_size, DMA_FROM_DEVICE);
		kfree(tun_qp->ring[i].addr);
	}
	kfree(tun_qp->ring);
	tun_qp->ring = NULL;
	return -ENOMEM;
}

static void mlx4_ib_free_pv_qp_bufs(struct mlx4_ib_demux_pv_ctx *ctx,
				     enum ib_qp_type qp_type, int is_tun)
{
	int i;
	struct mlx4_ib_demux_pv_qp *tun_qp;
	int rx_buf_size, tx_buf_size;

	if (qp_type > IB_QPT_GSI)
		return;

	tun_qp = &ctx->qp[qp_type];
	if (is_tun) {
		rx_buf_size = sizeof (struct mlx4_tunnel_mad);
		tx_buf_size = sizeof (struct mlx4_rcv_tunnel_mad);
	} else {
		rx_buf_size = sizeof (struct mlx4_mad_rcv_buf);
		tx_buf_size = sizeof (struct mlx4_mad_snd_buf);
	}


	for (i = 0; i < MLX4_NUM_TUNNEL_BUFS; i++) {
		ib_dma_unmap_single(ctx->ib_dev, tun_qp->ring[i].map,
				    rx_buf_size, DMA_FROM_DEVICE);
		kfree(tun_qp->ring[i].addr);
	}

	for (i = 0; i < MLX4_NUM_TUNNEL_BUFS; i++) {
		ib_dma_unmap_single(ctx->ib_dev, tun_qp->tx_ring[i].buf.map,
				    tx_buf_size, DMA_TO_DEVICE);
		kfree(tun_qp->tx_ring[i].buf.addr);
		if (tun_qp->tx_ring[i].ah)
			ib_destroy_ah(tun_qp->tx_ring[i].ah);
	}
	kfree(tun_qp->tx_ring);
	kfree(tun_qp->ring);
}

static void mlx4_ib_tunnel_comp_worker(struct work_struct *work)
{
	/* dummy until next patch in series */
}

static void pv_qp_event_handler(struct ib_event *event, void *qp_context)
{
	struct mlx4_ib_demux_pv_ctx *sqp = qp_context;

	/* It's worse than that! He's dead, Jim! */
	pr_err("Fatal error (%d) on a MAD QP on port %d\n",
	       event->event, sqp->port);
}

static int create_pv_sqp(struct mlx4_ib_demux_pv_ctx *ctx,
			    enum ib_qp_type qp_type, int create_tun)
{
	int i, ret;
	struct mlx4_ib_demux_pv_qp *tun_qp;
	struct mlx4_ib_qp_tunnel_init_attr qp_init_attr;
	struct ib_qp_attr attr;
	int qp_attr_mask_INIT;

	if (qp_type > IB_QPT_GSI)
		return -EINVAL;

	tun_qp = &ctx->qp[qp_type];

	memset(&qp_init_attr, 0, sizeof qp_init_attr);
	qp_init_attr.init_attr.send_cq = ctx->cq;
	qp_init_attr.init_attr.recv_cq = ctx->cq;
	qp_init_attr.init_attr.sq_sig_type = IB_SIGNAL_ALL_WR;
	qp_init_attr.init_attr.cap.max_send_wr = MLX4_NUM_TUNNEL_BUFS;
	qp_init_attr.init_attr.cap.max_recv_wr = MLX4_NUM_TUNNEL_BUFS;
	qp_init_attr.init_attr.cap.max_send_sge = 1;
	qp_init_attr.init_attr.cap.max_recv_sge = 1;
	if (create_tun) {
		qp_init_attr.init_attr.qp_type = IB_QPT_UD;
		qp_init_attr.init_attr.create_flags = MLX4_IB_SRIOV_TUNNEL_QP;
		qp_init_attr.port = ctx->port;
		qp_init_attr.slave = ctx->slave;
		qp_init_attr.proxy_qp_type = qp_type;
		qp_attr_mask_INIT = IB_QP_STATE | IB_QP_PKEY_INDEX |
			   IB_QP_QKEY | IB_QP_PORT;
	} else {
		qp_init_attr.init_attr.qp_type = qp_type;
		qp_init_attr.init_attr.create_flags = MLX4_IB_SRIOV_SQP;
		qp_attr_mask_INIT = IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_QKEY;
	}
	qp_init_attr.init_attr.port_num = ctx->port;
	qp_init_attr.init_attr.qp_context = ctx;
	qp_init_attr.init_attr.event_handler = pv_qp_event_handler;
	tun_qp->qp = ib_create_qp(ctx->pd, &qp_init_attr.init_attr);
	if (IS_ERR(tun_qp->qp)) {
		ret = PTR_ERR(tun_qp->qp);
		tun_qp->qp = NULL;
		pr_err("Couldn't create %s QP (%d)\n",
		       create_tun ? "tunnel" : "special", ret);
		return ret;
	}

	memset(&attr, 0, sizeof attr);
	attr.qp_state = IB_QPS_INIT;
	attr.pkey_index =
		to_mdev(ctx->ib_dev)->pkeys.virt2phys_pkey[ctx->slave][ctx->port - 1][0];
	attr.qkey = IB_QP1_QKEY;
	attr.port_num = ctx->port;
	ret = ib_modify_qp(tun_qp->qp, &attr, qp_attr_mask_INIT);
	if (ret) {
		pr_err("Couldn't change %s qp state to INIT (%d)\n",
		       create_tun ? "tunnel" : "special", ret);
		goto err_qp;
	}
	attr.qp_state = IB_QPS_RTR;
	ret = ib_modify_qp(tun_qp->qp, &attr, IB_QP_STATE);
	if (ret) {
		pr_err("Couldn't change %s qp state to RTR (%d)\n",
		       create_tun ? "tunnel" : "special", ret);
		goto err_qp;
	}
	attr.qp_state = IB_QPS_RTS;
	attr.sq_psn = 0;
	ret = ib_modify_qp(tun_qp->qp, &attr, IB_QP_STATE | IB_QP_SQ_PSN);
	if (ret) {
		pr_err("Couldn't change %s qp state to RTS (%d)\n",
		       create_tun ? "tunnel" : "special", ret);
		goto err_qp;
	}

	for (i = 0; i < MLX4_NUM_TUNNEL_BUFS; i++) {
		ret = mlx4_ib_post_pv_qp_buf(ctx, tun_qp, i);
		if (ret) {
			pr_err(" mlx4_ib_post_pv_buf error"
			       " (err = %d, i = %d)\n", ret, i);
			goto err_qp;
		}
	}
	return 0;

err_qp:
	ib_destroy_qp(tun_qp->qp);
	tun_qp->qp = NULL;
	return ret;
}

/*
 * IB MAD completion callback for real SQPs
 */
static void mlx4_ib_sqp_comp_worker(struct work_struct *work)
{
	/* dummy until next patch in series */
}

static int alloc_pv_object(struct mlx4_ib_dev *dev, int slave, int port,
			       struct mlx4_ib_demux_pv_ctx **ret_ctx)
{
	struct mlx4_ib_demux_pv_ctx *ctx;

	*ret_ctx = NULL;
	ctx = kzalloc(sizeof (struct mlx4_ib_demux_pv_ctx), GFP_KERNEL);
	if (!ctx) {
		pr_err("failed allocating pv resource context "
		       "for port %d, slave %d\n", port, slave);
		return -ENOMEM;
	}

	ctx->ib_dev = &dev->ib_dev;
	ctx->port = port;
	ctx->slave = slave;
	*ret_ctx = ctx;
	return 0;
}

static void free_pv_object(struct mlx4_ib_dev *dev, int slave, int port)
{
	if (dev->sriov.demux[port - 1].tun[slave]) {
		kfree(dev->sriov.demux[port - 1].tun[slave]);
		dev->sriov.demux[port - 1].tun[slave] = NULL;
	}
}

static int create_pv_resources(struct ib_device *ibdev, int slave, int port,
			       int create_tun, struct mlx4_ib_demux_pv_ctx *ctx)
{
	int ret, cq_size;

	ctx->state = DEMUX_PV_STATE_STARTING;
	/* have QP0 only on port owner, and only if link layer is IB */
	if (ctx->slave == mlx4_master_func_num(to_mdev(ctx->ib_dev)->dev) &&
	    rdma_port_get_link_layer(ibdev, ctx->port) == IB_LINK_LAYER_INFINIBAND)
		ctx->has_smi = 1;

	if (ctx->has_smi) {
		ret = mlx4_ib_alloc_pv_bufs(ctx, IB_QPT_SMI, create_tun);
		if (ret) {
			pr_err("Failed allocating qp0 tunnel bufs (%d)\n", ret);
			goto err_out;
		}
	}

	ret = mlx4_ib_alloc_pv_bufs(ctx, IB_QPT_GSI, create_tun);
	if (ret) {
		pr_err("Failed allocating qp1 tunnel bufs (%d)\n", ret);
		goto err_out_qp0;
	}

	cq_size = 2 * MLX4_NUM_TUNNEL_BUFS;
	if (ctx->has_smi)
		cq_size *= 2;

	ctx->cq = ib_create_cq(ctx->ib_dev, mlx4_ib_tunnel_comp_handler,
			       NULL, ctx, cq_size, 0);
	if (IS_ERR(ctx->cq)) {
		ret = PTR_ERR(ctx->cq);
		pr_err("Couldn't create tunnel CQ (%d)\n", ret);
		goto err_buf;
	}

	ctx->pd = ib_alloc_pd(ctx->ib_dev);
	if (IS_ERR(ctx->pd)) {
		ret = PTR_ERR(ctx->pd);
		pr_err("Couldn't create tunnel PD (%d)\n", ret);
		goto err_cq;
	}

	ctx->mr = ib_get_dma_mr(ctx->pd, IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(ctx->mr)) {
		ret = PTR_ERR(ctx->mr);
		pr_err("Couldn't get tunnel DMA MR (%d)\n", ret);
		goto err_pd;
	}

	if (ctx->has_smi) {
		ret = create_pv_sqp(ctx, IB_QPT_SMI, create_tun);
		if (ret) {
			pr_err("Couldn't create %s QP0 (%d)\n",
			       create_tun ? "tunnel for" : "",  ret);
			goto err_mr;
		}
	}

	ret = create_pv_sqp(ctx, IB_QPT_GSI, create_tun);
	if (ret) {
		pr_err("Couldn't create %s QP1 (%d)\n",
		       create_tun ? "tunnel for" : "",  ret);
		goto err_qp0;
	}

	if (create_tun)
		INIT_WORK(&ctx->work, mlx4_ib_tunnel_comp_worker);
	else
		INIT_WORK(&ctx->work, mlx4_ib_sqp_comp_worker);

	ctx->wq = to_mdev(ibdev)->sriov.demux[port - 1].wq;

	ret = ib_req_notify_cq(ctx->cq, IB_CQ_NEXT_COMP);
	if (ret) {
		pr_err("Couldn't arm tunnel cq (%d)\n", ret);
		goto err_wq;
	}
	ctx->state = DEMUX_PV_STATE_ACTIVE;
	return 0;

err_wq:
	ctx->wq = NULL;
	ib_destroy_qp(ctx->qp[1].qp);
	ctx->qp[1].qp = NULL;


err_qp0:
	if (ctx->has_smi)
		ib_destroy_qp(ctx->qp[0].qp);
	ctx->qp[0].qp = NULL;

err_mr:
	ib_dereg_mr(ctx->mr);
	ctx->mr = NULL;

err_pd:
	ib_dealloc_pd(ctx->pd);
	ctx->pd = NULL;

err_cq:
	ib_destroy_cq(ctx->cq);
	ctx->cq = NULL;

err_buf:
	mlx4_ib_free_pv_qp_bufs(ctx, IB_QPT_GSI, create_tun);

err_out_qp0:
	if (ctx->has_smi)
		mlx4_ib_free_pv_qp_bufs(ctx, IB_QPT_SMI, create_tun);
err_out:
	ctx->state = DEMUX_PV_STATE_DOWN;
	return ret;
}

static void destroy_pv_resources(struct mlx4_ib_dev *dev, int slave, int port,
				 struct mlx4_ib_demux_pv_ctx *ctx, int flush)
{
	if (!ctx)
		return;
	if (ctx->state > DEMUX_PV_STATE_DOWN) {
		ctx->state = DEMUX_PV_STATE_DOWNING;
		if (flush)
			flush_workqueue(ctx->wq);
		if (ctx->has_smi) {
			ib_destroy_qp(ctx->qp[0].qp);
			ctx->qp[0].qp = NULL;
			mlx4_ib_free_pv_qp_bufs(ctx, IB_QPT_SMI, 1);
		}
		ib_destroy_qp(ctx->qp[1].qp);
		ctx->qp[1].qp = NULL;
		mlx4_ib_free_pv_qp_bufs(ctx, IB_QPT_GSI, 1);
		ib_dereg_mr(ctx->mr);
		ctx->mr = NULL;
		ib_dealloc_pd(ctx->pd);
		ctx->pd = NULL;
		ib_destroy_cq(ctx->cq);
		ctx->cq = NULL;
		ctx->state = DEMUX_PV_STATE_DOWN;
	}
}

static int mlx4_ib_tunnels_update(struct mlx4_ib_dev *dev, int slave,
				  int port, int do_init)
{
	int ret = 0;

	if (!do_init) {
		/* for master, destroy real sqp resources */
		if (slave == mlx4_master_func_num(dev->dev))
			destroy_pv_resources(dev, slave, port,
					     dev->sriov.sqps[port - 1], 1);
		/* destroy the tunnel qp resources */
		destroy_pv_resources(dev, slave, port,
				     dev->sriov.demux[port - 1].tun[slave], 1);
		return 0;
	}

	/* create the tunnel qp resources */
	ret = create_pv_resources(&dev->ib_dev, slave, port, 1,
				  dev->sriov.demux[port - 1].tun[slave]);

	/* for master, create the real sqp resources */
	if (!ret && slave == mlx4_master_func_num(dev->dev))
		ret = create_pv_resources(&dev->ib_dev, slave, port, 0,
					  dev->sriov.sqps[port - 1]);
	return ret;
}

void mlx4_ib_tunnels_update_work(struct work_struct *work)
{
	struct mlx4_ib_demux_work *dmxw;

	dmxw = container_of(work, struct mlx4_ib_demux_work, work);
	mlx4_ib_tunnels_update(dmxw->dev, dmxw->slave, (int) dmxw->port,
			       dmxw->do_init);
	kfree(dmxw);
	return;
}

static int mlx4_ib_alloc_demux_ctx(struct mlx4_ib_dev *dev,
				       struct mlx4_ib_demux_ctx *ctx,
				       int port)
{
	char name[12];
	int ret = 0;
	int i;

	ctx->tun = kcalloc(dev->dev->caps.sqp_demux,
			   sizeof (struct mlx4_ib_demux_pv_ctx *), GFP_KERNEL);
	if (!ctx->tun)
		return -ENOMEM;

	ctx->dev = dev;
	ctx->port = port;
	ctx->ib_dev = &dev->ib_dev;

	for (i = 0; i < dev->dev->caps.sqp_demux; i++) {
		ret = alloc_pv_object(dev, i, port, &ctx->tun[i]);
		if (ret) {
			ret = -ENOMEM;
			goto err_wq;
		}
	}

	snprintf(name, sizeof name, "mlx4_ibt%d", port);
	ctx->wq = create_singlethread_workqueue(name);
	if (!ctx->wq) {
		pr_err("Failed to create tunnelling WQ for port %d\n", port);
		ret = -ENOMEM;
		goto err_wq;
	}

	snprintf(name, sizeof name, "mlx4_ibud%d", port);
	ctx->ud_wq = create_singlethread_workqueue(name);
	if (!ctx->ud_wq) {
		pr_err("Failed to create up/down WQ for port %d\n", port);
		ret = -ENOMEM;
		goto err_udwq;
	}

	return 0;

err_udwq:
	destroy_workqueue(ctx->wq);
	ctx->wq = NULL;

err_wq:
	for (i = 0; i < dev->dev->caps.sqp_demux; i++)
		free_pv_object(dev, i, port);
	kfree(ctx->tun);
	ctx->tun = NULL;
	return ret;
}

static void mlx4_ib_free_sqp_ctx(struct mlx4_ib_demux_pv_ctx *sqp_ctx)
{
	if (sqp_ctx->state > DEMUX_PV_STATE_DOWN) {
		sqp_ctx->state = DEMUX_PV_STATE_DOWNING;
		flush_workqueue(sqp_ctx->wq);
		if (sqp_ctx->has_smi) {
			ib_destroy_qp(sqp_ctx->qp[0].qp);
			sqp_ctx->qp[0].qp = NULL;
			mlx4_ib_free_pv_qp_bufs(sqp_ctx, IB_QPT_SMI, 0);
		}
		ib_destroy_qp(sqp_ctx->qp[1].qp);
		sqp_ctx->qp[1].qp = NULL;
		mlx4_ib_free_pv_qp_bufs(sqp_ctx, IB_QPT_GSI, 0);
		ib_dereg_mr(sqp_ctx->mr);
		sqp_ctx->mr = NULL;
		ib_dealloc_pd(sqp_ctx->pd);
		sqp_ctx->pd = NULL;
		ib_destroy_cq(sqp_ctx->cq);
		sqp_ctx->cq = NULL;
		sqp_ctx->state = DEMUX_PV_STATE_DOWN;
	}
}

static void mlx4_ib_free_demux_ctx(struct mlx4_ib_demux_ctx *ctx)
{
	int i;
	if (ctx) {
		struct mlx4_ib_dev *dev = to_mdev(ctx->ib_dev);
		for (i = 0; i < dev->dev->caps.sqp_demux; i++) {
			if (!ctx->tun[i])
				continue;
			if (ctx->tun[i]->state > DEMUX_PV_STATE_DOWN)
				ctx->tun[i]->state = DEMUX_PV_STATE_DOWNING;
		}
		flush_workqueue(ctx->wq);
		for (i = 0; i < dev->dev->caps.sqp_demux; i++) {
			destroy_pv_resources(dev, i, ctx->port, ctx->tun[i], 0);
			free_pv_object(dev, i, ctx->port);
		}
		kfree(ctx->tun);
		destroy_workqueue(ctx->ud_wq);
		destroy_workqueue(ctx->wq);
	}
}

static void mlx4_ib_master_tunnels(struct mlx4_ib_dev *dev, int do_init)
{
	int i;

	if (!mlx4_is_master(dev->dev))
		return;
	/* initialize or tear down tunnel QPs for the master */
	for (i = 0; i < dev->dev->caps.num_ports; i++)
		mlx4_ib_tunnels_update(dev, mlx4_master_func_num(dev->dev), i + 1, do_init);
	return;
}

int mlx4_ib_init_sriov(struct mlx4_ib_dev *dev)
{
	int i = 0;
	int err;

	if (!mlx4_is_mfunc(dev->dev))
		return 0;

	dev->sriov.is_going_down = 0;
	spin_lock_init(&dev->sriov.going_down_lock);

	mlx4_ib_warn(&dev->ib_dev, "multi-function enabled\n");

	if (mlx4_is_slave(dev->dev)) {
		mlx4_ib_warn(&dev->ib_dev, "operating in qp1 tunnel mode\n");
		return 0;
	}

	mlx4_ib_warn(&dev->ib_dev, "initializing demux service for %d qp1 clients\n",
		     dev->dev->caps.sqp_demux);
	for (i = 0; i < dev->num_ports; i++) {
		err = alloc_pv_object(dev, mlx4_master_func_num(dev->dev), i + 1,
				      &dev->sriov.sqps[i]);
		if (err)
			goto demux_err;
		err = mlx4_ib_alloc_demux_ctx(dev, &dev->sriov.demux[i], i + 1);
		if (err)
			goto demux_err;
	}
	mlx4_ib_master_tunnels(dev, 1);
	return 0;

demux_err:
	while (i > 0) {
		free_pv_object(dev, mlx4_master_func_num(dev->dev), i + 1);
		mlx4_ib_free_demux_ctx(&dev->sriov.demux[i]);
		--i;
	}

	return err;
}

void mlx4_ib_close_sriov(struct mlx4_ib_dev *dev)
{
	int i;
	unsigned long flags;

	if (!mlx4_is_mfunc(dev->dev))
		return;

	spin_lock_irqsave(&dev->sriov.going_down_lock, flags);
	dev->sriov.is_going_down = 1;
	spin_unlock_irqrestore(&dev->sriov.going_down_lock, flags);
	if (mlx4_is_master(dev->dev))
		for (i = 0; i < dev->num_ports; i++) {
			flush_workqueue(dev->sriov.demux[i].ud_wq);
			mlx4_ib_free_sqp_ctx(dev->sriov.sqps[i]);
			kfree(dev->sriov.sqps[i]);
			dev->sriov.sqps[i] = NULL;
			mlx4_ib_free_demux_ctx(&dev->sriov.demux[i]);
		}
}
