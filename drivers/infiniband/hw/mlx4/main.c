/*
 * Copyright (c) 2006, 2007 Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <net/ipv6.h>
#include <net/addrconf.h>

#include <rdma/ib_smi.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_addr.h>

#include <linux/mlx4/driver.h>
#include <linux/mlx4/cmd.h>
#include <linux/mlx4/qp.h>

#include "mlx4_ib.h"
#include "user.h"

#define DRV_NAME	MLX4_IB_DRV_NAME
#define DRV_VERSION	"2.2-1"
#define DRV_RELDATE	"Feb 2014"

#define MLX4_IB_FLOW_MAX_PRIO 0xFFF
#define MLX4_IB_FLOW_QPN_MASK 0xFFFFFF
#define MLX4_IB_CARD_REV_A0   0xA0

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("Mellanox ConnectX HCA InfiniBand driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);

int mlx4_ib_sm_guid_assign = 1;
module_param_named(sm_guid_assign, mlx4_ib_sm_guid_assign, int, 0444);
MODULE_PARM_DESC(sm_guid_assign, "Enable SM alias_GUID assignment if sm_guid_assign > 0 (Default: 1)");

static const char mlx4_ib_version[] =
	DRV_NAME ": Mellanox ConnectX InfiniBand driver v"
	DRV_VERSION " (" DRV_RELDATE ")\n";

struct update_gid_work {
	struct work_struct	work;
	union ib_gid		gids[128];
	struct mlx4_ib_dev     *dev;
	int			port;
};

static void do_slave_init(struct mlx4_ib_dev *ibdev, int slave, int do_init);

static struct workqueue_struct *wq;

static void init_query_mad(struct ib_smp *mad)
{
	mad->base_version  = 1;
	mad->mgmt_class    = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	mad->class_version = 1;
	mad->method	   = IB_MGMT_METHOD_GET;
}

static union ib_gid zgid;

static int check_flow_steering_support(struct mlx4_dev *dev)
{
	int eth_num_ports = 0;
	int ib_num_ports = 0;

	int dmfs = dev->caps.steering_mode == MLX4_STEERING_MODE_DEVICE_MANAGED;

	if (dmfs) {
		int i;
		mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_ETH)
			eth_num_ports++;
		mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_IB)
			ib_num_ports++;
		dmfs &= (!ib_num_ports ||
			 (dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_DMFS_IPOIB)) &&
			(!eth_num_ports ||
			 (dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_FS_EN));
		if (ib_num_ports && mlx4_is_mfunc(dev)) {
			pr_warn("Device managed flow steering is unavailable for IB port in multifunction env.\n");
			dmfs = 0;
		}
	}
	return dmfs;
}

static int num_ib_ports(struct mlx4_dev *dev)
{
	int ib_ports = 0;
	int i;

	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_IB)
		ib_ports++;

	return ib_ports;
}

static int mlx4_ib_query_device(struct ib_device *ibdev,
				struct ib_device_attr *props)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	int have_ib_ports;

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id = IB_SMP_ATTR_NODE_INFO;

	err = mlx4_MAD_IFC(to_mdev(ibdev), MLX4_MAD_IFC_IGNORE_KEYS,
			   1, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	memset(props, 0, sizeof *props);

	have_ib_ports = num_ib_ports(dev->dev);

	props->fw_ver = dev->dev->caps.fw_ver;
	props->device_cap_flags    = IB_DEVICE_CHANGE_PHY_PORT |
		IB_DEVICE_PORT_ACTIVE_EVENT		|
		IB_DEVICE_SYS_IMAGE_GUID		|
		IB_DEVICE_RC_RNR_NAK_GEN		|
		IB_DEVICE_BLOCK_MULTICAST_LOOPBACK;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_BAD_PKEY_CNTR)
		props->device_cap_flags |= IB_DEVICE_BAD_PKEY_CNTR;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_BAD_QKEY_CNTR)
		props->device_cap_flags |= IB_DEVICE_BAD_QKEY_CNTR;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_APM && have_ib_ports)
		props->device_cap_flags |= IB_DEVICE_AUTO_PATH_MIG;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_UD_AV_PORT)
		props->device_cap_flags |= IB_DEVICE_UD_AV_PORT_ENFORCE;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_IPOIB_CSUM)
		props->device_cap_flags |= IB_DEVICE_UD_IP_CSUM;
	if (dev->dev->caps.max_gso_sz &&
	    (dev->dev->rev_id != MLX4_IB_CARD_REV_A0) &&
	    (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_BLH))
		props->device_cap_flags |= IB_DEVICE_UD_TSO;
	if (dev->dev->caps.bmme_flags & MLX4_BMME_FLAG_RESERVED_LKEY)
		props->device_cap_flags |= IB_DEVICE_LOCAL_DMA_LKEY;
	if ((dev->dev->caps.bmme_flags & MLX4_BMME_FLAG_LOCAL_INV) &&
	    (dev->dev->caps.bmme_flags & MLX4_BMME_FLAG_REMOTE_INV) &&
	    (dev->dev->caps.bmme_flags & MLX4_BMME_FLAG_FAST_REG_WR))
		props->device_cap_flags |= IB_DEVICE_MEM_MGT_EXTENSIONS;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC)
		props->device_cap_flags |= IB_DEVICE_XRC;
	if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_MEM_WINDOW)
		props->device_cap_flags |= IB_DEVICE_MEM_WINDOW;
	if (dev->dev->caps.bmme_flags & MLX4_BMME_FLAG_TYPE_2_WIN) {
		if (dev->dev->caps.bmme_flags & MLX4_BMME_FLAG_WIN_TYPE_2B)
			props->device_cap_flags |= IB_DEVICE_MEM_WINDOW_TYPE_2B;
		else
			props->device_cap_flags |= IB_DEVICE_MEM_WINDOW_TYPE_2A;
	if (dev->steering_support ==  MLX4_STEERING_MODE_DEVICE_MANAGED)
		props->device_cap_flags |= IB_DEVICE_MANAGED_FLOW_STEERING;
	}

	props->vendor_id	   = be32_to_cpup((__be32 *) (out_mad->data + 36)) &
		0xffffff;
	props->vendor_part_id	   = dev->dev->pdev->device;
	props->hw_ver		   = be32_to_cpup((__be32 *) (out_mad->data + 32));
	memcpy(&props->sys_image_guid, out_mad->data +	4, 8);

	props->max_mr_size	   = ~0ull;
	props->page_size_cap	   = dev->dev->caps.page_size_cap;
	props->max_qp		   = dev->dev->quotas.qp;
	props->max_qp_wr	   = dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE;
	props->max_sge		   = min(dev->dev->caps.max_sq_sg,
					 dev->dev->caps.max_rq_sg);
	props->max_cq		   = dev->dev->quotas.cq;
	props->max_cqe		   = dev->dev->caps.max_cqes;
	props->max_mr		   = dev->dev->quotas.mpt;
	props->max_pd		   = dev->dev->caps.num_pds - dev->dev->caps.reserved_pds;
	props->max_qp_rd_atom	   = dev->dev->caps.max_qp_dest_rdma;
	props->max_qp_init_rd_atom = dev->dev->caps.max_qp_init_rdma;
	props->max_res_rd_atom	   = props->max_qp_rd_atom * props->max_qp;
	props->max_srq		   = dev->dev->quotas.srq;
	props->max_srq_wr	   = dev->dev->caps.max_srq_wqes - 1;
	props->max_srq_sge	   = dev->dev->caps.max_srq_sge;
	props->max_fast_reg_page_list_len = MLX4_MAX_FAST_REG_PAGES;
	props->local_ca_ack_delay  = dev->dev->caps.local_ca_ack_delay;
	props->atomic_cap	   = dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_ATOMIC ?
		IB_ATOMIC_HCA : IB_ATOMIC_NONE;
	props->masked_atomic_cap   = props->atomic_cap;
	props->max_pkeys	   = dev->dev->caps.pkey_table_len[1];
	props->max_mcast_grp	   = dev->dev->caps.num_mgms + dev->dev->caps.num_amgms;
	props->max_mcast_qp_attach = dev->dev->caps.num_qp_per_mgm;
	props->max_total_mcast_qp_attach = props->max_mcast_qp_attach *
					   props->max_mcast_grp;
	props->max_map_per_fmr = dev->dev->caps.max_fmr_maps;

out:
	kfree(in_mad);
	kfree(out_mad);

	return err;
}

static enum rdma_link_layer
mlx4_ib_port_link_layer(struct ib_device *device, u8 port_num)
{
	struct mlx4_dev *dev = to_mdev(device)->dev;

	return dev->caps.port_mask[port_num] == MLX4_PORT_TYPE_IB ?
		IB_LINK_LAYER_INFINIBAND : IB_LINK_LAYER_ETHERNET;
}

static int ib_link_query_port(struct ib_device *ibdev, u8 port,
			      struct ib_port_attr *props, int netw_view)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int ext_active_speed;
	int mad_ifc_flags = MLX4_MAD_IFC_IGNORE_KEYS;
	int err = -ENOMEM;

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod = cpu_to_be32(port);

	if (mlx4_is_mfunc(to_mdev(ibdev)->dev) && netw_view)
		mad_ifc_flags |= MLX4_MAD_IFC_NET_VIEW;

	err = mlx4_MAD_IFC(to_mdev(ibdev), mad_ifc_flags, port, NULL, NULL,
				in_mad, out_mad);
	if (err)
		goto out;


	props->lid		= be16_to_cpup((__be16 *) (out_mad->data + 16));
	props->lmc		= out_mad->data[34] & 0x7;
	props->sm_lid		= be16_to_cpup((__be16 *) (out_mad->data + 18));
	props->sm_sl		= out_mad->data[36] & 0xf;
	props->state		= out_mad->data[32] & 0xf;
	props->phys_state	= out_mad->data[33] >> 4;
	props->port_cap_flags	= be32_to_cpup((__be32 *) (out_mad->data + 20));
	if (netw_view)
		props->gid_tbl_len = out_mad->data[50];
	else
		props->gid_tbl_len = to_mdev(ibdev)->dev->caps.gid_table_len[port];
	props->max_msg_sz	= to_mdev(ibdev)->dev->caps.max_msg_sz;
	props->pkey_tbl_len	= to_mdev(ibdev)->dev->caps.pkey_table_len[port];
	props->bad_pkey_cntr	= be16_to_cpup((__be16 *) (out_mad->data + 46));
	props->qkey_viol_cntr	= be16_to_cpup((__be16 *) (out_mad->data + 48));
	props->active_width	= out_mad->data[31] & 0xf;
	props->active_speed	= out_mad->data[35] >> 4;
	props->max_mtu		= out_mad->data[41] & 0xf;
	props->active_mtu	= out_mad->data[36] >> 4;
	props->subnet_timeout	= out_mad->data[51] & 0x1f;
	props->max_vl_num	= out_mad->data[37] >> 4;
	props->init_type_reply	= out_mad->data[41] >> 4;

	/* Check if extended speeds (EDR/FDR/...) are supported */
	if (props->port_cap_flags & IB_PORT_EXTENDED_SPEEDS_SUP) {
		ext_active_speed = out_mad->data[62] >> 4;

		switch (ext_active_speed) {
		case 1:
			props->active_speed = IB_SPEED_FDR;
			break;
		case 2:
			props->active_speed = IB_SPEED_EDR;
			break;
		}
	}

	/* If reported active speed is QDR, check if is FDR-10 */
	if (props->active_speed == IB_SPEED_QDR) {
		init_query_mad(in_mad);
		in_mad->attr_id = MLX4_ATTR_EXTENDED_PORT_INFO;
		in_mad->attr_mod = cpu_to_be32(port);

		err = mlx4_MAD_IFC(to_mdev(ibdev), mad_ifc_flags, port,
				   NULL, NULL, in_mad, out_mad);
		if (err)
			goto out;

		/* Checking LinkSpeedActive for FDR-10 */
		if (out_mad->data[15] & 0x1)
			props->active_speed = IB_SPEED_FDR10;
	}

	/* Avoid wrong speed value returned by FW if the IB link is down. */
	if (props->state == IB_PORT_DOWN)
		 props->active_speed = IB_SPEED_SDR;

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static u8 state_to_phys_state(enum ib_port_state state)
{
	return state == IB_PORT_ACTIVE ? 5 : 3;
}

static int eth_link_query_port(struct ib_device *ibdev, u8 port,
			       struct ib_port_attr *props, int netw_view)
{

	struct mlx4_ib_dev *mdev = to_mdev(ibdev);
	struct mlx4_ib_iboe *iboe = &mdev->iboe;
	struct net_device *ndev;
	enum ib_mtu tmp;
	struct mlx4_cmd_mailbox *mailbox;
	int err = 0;

	mailbox = mlx4_alloc_cmd_mailbox(mdev->dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	err = mlx4_cmd_box(mdev->dev, 0, mailbox->dma, port, 0,
			   MLX4_CMD_QUERY_PORT, MLX4_CMD_TIME_CLASS_B,
			   MLX4_CMD_WRAPPED);
	if (err)
		goto out;

	props->active_width	=  (((u8 *)mailbox->buf)[5] == 0x40) ?
						IB_WIDTH_4X : IB_WIDTH_1X;
	props->active_speed	= IB_SPEED_QDR;
	props->port_cap_flags	= IB_PORT_CM_SUP | IB_PORT_IP_BASED_GIDS;
	props->gid_tbl_len	= mdev->dev->caps.gid_table_len[port];
	props->max_msg_sz	= mdev->dev->caps.max_msg_sz;
	props->pkey_tbl_len	= 1;
	props->max_mtu		= IB_MTU_4096;
	props->max_vl_num	= 2;
	props->state		= IB_PORT_DOWN;
	props->phys_state	= state_to_phys_state(props->state);
	props->active_mtu	= IB_MTU_256;
	spin_lock_bh(&iboe->lock);
	ndev = iboe->netdevs[port - 1];
	if (!ndev)
		goto out_unlock;

	tmp = iboe_get_mtu(ndev->mtu);
	props->active_mtu = tmp ? min(props->max_mtu, tmp) : IB_MTU_256;

	props->state		= (netif_running(ndev) && netif_carrier_ok(ndev)) ?
					IB_PORT_ACTIVE : IB_PORT_DOWN;
	props->phys_state	= state_to_phys_state(props->state);
out_unlock:
	spin_unlock_bh(&iboe->lock);
out:
	mlx4_free_cmd_mailbox(mdev->dev, mailbox);
	return err;
}

int __mlx4_ib_query_port(struct ib_device *ibdev, u8 port,
			 struct ib_port_attr *props, int netw_view)
{
	int err;

	memset(props, 0, sizeof *props);

	err = mlx4_ib_port_link_layer(ibdev, port) == IB_LINK_LAYER_INFINIBAND ?
		ib_link_query_port(ibdev, port, props, netw_view) :
				eth_link_query_port(ibdev, port, props, netw_view);

	return err;
}

static int mlx4_ib_query_port(struct ib_device *ibdev, u8 port,
			      struct ib_port_attr *props)
{
	/* returns host view */
	return __mlx4_ib_query_port(ibdev, port, props, 0);
}

int __mlx4_ib_query_gid(struct ib_device *ibdev, u8 port, int index,
			union ib_gid *gid, int netw_view)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err = -ENOMEM;
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	int clear = 0;
	int mad_ifc_flags = MLX4_MAD_IFC_IGNORE_KEYS;

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PORT_INFO;
	in_mad->attr_mod = cpu_to_be32(port);

	if (mlx4_is_mfunc(dev->dev) && netw_view)
		mad_ifc_flags |= MLX4_MAD_IFC_NET_VIEW;

	err = mlx4_MAD_IFC(dev, mad_ifc_flags, port, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	memcpy(gid->raw, out_mad->data + 8, 8);

	if (mlx4_is_mfunc(dev->dev) && !netw_view) {
		if (index) {
			/* For any index > 0, return the null guid */
			err = 0;
			clear = 1;
			goto out;
		}
	}

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_GUID_INFO;
	in_mad->attr_mod = cpu_to_be32(index / 8);

	err = mlx4_MAD_IFC(dev, mad_ifc_flags, port,
			   NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	memcpy(gid->raw + 8, out_mad->data + (index % 8) * 8, 8);

out:
	if (clear)
		memset(gid->raw + 8, 0, 8);
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static int iboe_query_gid(struct ib_device *ibdev, u8 port, int index,
			  union ib_gid *gid)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);

	*gid = dev->iboe.gid_table[port - 1][index];

	return 0;
}

static int mlx4_ib_query_gid(struct ib_device *ibdev, u8 port, int index,
			     union ib_gid *gid)
{
	if (rdma_port_get_link_layer(ibdev, port) == IB_LINK_LAYER_INFINIBAND)
		return __mlx4_ib_query_gid(ibdev, port, index, gid, 0);
	else
		return iboe_query_gid(ibdev, port, index, gid);
}

int __mlx4_ib_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
			 u16 *pkey, int netw_view)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int mad_ifc_flags = MLX4_MAD_IFC_IGNORE_KEYS;
	int err = -ENOMEM;

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_PKEY_TABLE;
	in_mad->attr_mod = cpu_to_be32(index / 32);

	if (mlx4_is_mfunc(to_mdev(ibdev)->dev) && netw_view)
		mad_ifc_flags |= MLX4_MAD_IFC_NET_VIEW;

	err = mlx4_MAD_IFC(to_mdev(ibdev), mad_ifc_flags, port, NULL, NULL,
			   in_mad, out_mad);
	if (err)
		goto out;

	*pkey = be16_to_cpu(((__be16 *) out_mad->data)[index % 32]);

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static int mlx4_ib_query_pkey(struct ib_device *ibdev, u8 port, u16 index, u16 *pkey)
{
	return __mlx4_ib_query_pkey(ibdev, port, index, pkey, 0);
}

static int mlx4_ib_modify_device(struct ib_device *ibdev, int mask,
				 struct ib_device_modify *props)
{
	struct mlx4_cmd_mailbox *mailbox;
	unsigned long flags;

	if (mask & ~IB_DEVICE_MODIFY_NODE_DESC)
		return -EOPNOTSUPP;

	if (!(mask & IB_DEVICE_MODIFY_NODE_DESC))
		return 0;

	if (mlx4_is_slave(to_mdev(ibdev)->dev))
		return -EOPNOTSUPP;

	spin_lock_irqsave(&to_mdev(ibdev)->sm_lock, flags);
	memcpy(ibdev->node_desc, props->node_desc, 64);
	spin_unlock_irqrestore(&to_mdev(ibdev)->sm_lock, flags);

	/*
	 * If possible, pass node desc to FW, so it can generate
	 * a 144 trap.  If cmd fails, just ignore.
	 */
	mailbox = mlx4_alloc_cmd_mailbox(to_mdev(ibdev)->dev);
	if (IS_ERR(mailbox))
		return 0;

	memcpy(mailbox->buf, props->node_desc, 64);
	mlx4_cmd(to_mdev(ibdev)->dev, mailbox->dma, 1, 0,
		 MLX4_CMD_SET_NODE, MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(to_mdev(ibdev)->dev, mailbox);

	return 0;
}

static int mlx4_ib_SET_PORT(struct mlx4_ib_dev *dev, u8 port, int reset_qkey_viols,
			    u32 cap_mask)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err;

	mailbox = mlx4_alloc_cmd_mailbox(dev->dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	if (dev->dev->flags & MLX4_FLAG_OLD_PORT_CMDS) {
		*(u8 *) mailbox->buf	     = !!reset_qkey_viols << 6;
		((__be32 *) mailbox->buf)[2] = cpu_to_be32(cap_mask);
	} else {
		((u8 *) mailbox->buf)[3]     = !!reset_qkey_viols;
		((__be32 *) mailbox->buf)[1] = cpu_to_be32(cap_mask);
	}

	err = mlx4_cmd(dev->dev, mailbox->dma, port, 0, MLX4_CMD_SET_PORT,
		       MLX4_CMD_TIME_CLASS_B, MLX4_CMD_WRAPPED);

	mlx4_free_cmd_mailbox(dev->dev, mailbox);
	return err;
}

static int mlx4_ib_modify_port(struct ib_device *ibdev, u8 port, int mask,
			       struct ib_port_modify *props)
{
	struct mlx4_ib_dev *mdev = to_mdev(ibdev);
	u8 is_eth = mdev->dev->caps.port_type[port] == MLX4_PORT_TYPE_ETH;
	struct ib_port_attr attr;
	u32 cap_mask;
	int err;

	/* return OK if this is RoCE. CM calls ib_modify_port() regardless
	 * of whether port link layer is ETH or IB. For ETH ports, qkey
	 * violations and port capabilities are not meaningful.
	 */
	if (is_eth)
		return 0;

	mutex_lock(&mdev->cap_mask_mutex);

	err = mlx4_ib_query_port(ibdev, port, &attr);
	if (err)
		goto out;

	cap_mask = (attr.port_cap_flags | props->set_port_cap_mask) &
		~props->clr_port_cap_mask;

	err = mlx4_ib_SET_PORT(mdev, port,
			       !!(mask & IB_PORT_RESET_QKEY_CNTR),
			       cap_mask);

out:
	mutex_unlock(&to_mdev(ibdev)->cap_mask_mutex);
	return err;
}

static struct ib_ucontext *mlx4_ib_alloc_ucontext(struct ib_device *ibdev,
						  struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_ib_ucontext *context;
	struct mlx4_ib_alloc_ucontext_resp_v3 resp_v3;
	struct mlx4_ib_alloc_ucontext_resp resp;
	int err;

	if (!dev->ib_active)
		return ERR_PTR(-EAGAIN);

	if (ibdev->uverbs_abi_ver == MLX4_IB_UVERBS_NO_DEV_CAPS_ABI_VERSION) {
		resp_v3.qp_tab_size      = dev->dev->caps.num_qps;
		resp_v3.bf_reg_size      = dev->dev->caps.bf_reg_size;
		resp_v3.bf_regs_per_page = dev->dev->caps.bf_regs_per_page;
	} else {
		resp.dev_caps	      = dev->dev->caps.userspace_caps;
		resp.qp_tab_size      = dev->dev->caps.num_qps;
		resp.bf_reg_size      = dev->dev->caps.bf_reg_size;
		resp.bf_regs_per_page = dev->dev->caps.bf_regs_per_page;
		resp.cqe_size	      = dev->dev->caps.cqe_size;
	}

	context = kmalloc(sizeof *context, GFP_KERNEL);
	if (!context)
		return ERR_PTR(-ENOMEM);

	err = mlx4_uar_alloc(to_mdev(ibdev)->dev, &context->uar);
	if (err) {
		kfree(context);
		return ERR_PTR(err);
	}

	INIT_LIST_HEAD(&context->db_page_list);
	mutex_init(&context->db_page_mutex);

	if (ibdev->uverbs_abi_ver == MLX4_IB_UVERBS_NO_DEV_CAPS_ABI_VERSION)
		err = ib_copy_to_udata(udata, &resp_v3, sizeof(resp_v3));
	else
		err = ib_copy_to_udata(udata, &resp, sizeof(resp));

	if (err) {
		mlx4_uar_free(to_mdev(ibdev)->dev, &context->uar);
		kfree(context);
		return ERR_PTR(-EFAULT);
	}

	return &context->ibucontext;
}

static int mlx4_ib_dealloc_ucontext(struct ib_ucontext *ibcontext)
{
	struct mlx4_ib_ucontext *context = to_mucontext(ibcontext);

	mlx4_uar_free(to_mdev(ibcontext->device)->dev, &context->uar);
	kfree(context);

	return 0;
}

static int mlx4_ib_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	struct mlx4_ib_dev *dev = to_mdev(context->device);

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	if (vma->vm_pgoff == 0) {
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

		if (io_remap_pfn_range(vma, vma->vm_start,
				       to_mucontext(context)->uar.pfn,
				       PAGE_SIZE, vma->vm_page_prot))
			return -EAGAIN;
	} else if (vma->vm_pgoff == 1 && dev->dev->caps.bf_reg_size != 0) {
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

		if (io_remap_pfn_range(vma, vma->vm_start,
				       to_mucontext(context)->uar.pfn +
				       dev->dev->caps.num_uars,
				       PAGE_SIZE, vma->vm_page_prot))
			return -EAGAIN;
	} else
		return -EINVAL;

	return 0;
}

static struct ib_pd *mlx4_ib_alloc_pd(struct ib_device *ibdev,
				      struct ib_ucontext *context,
				      struct ib_udata *udata)
{
	struct mlx4_ib_pd *pd;
	int err;

	pd = kmalloc(sizeof *pd, GFP_KERNEL);
	if (!pd)
		return ERR_PTR(-ENOMEM);

	err = mlx4_pd_alloc(to_mdev(ibdev)->dev, &pd->pdn);
	if (err) {
		kfree(pd);
		return ERR_PTR(err);
	}

	if (context)
		if (ib_copy_to_udata(udata, &pd->pdn, sizeof (__u32))) {
			mlx4_pd_free(to_mdev(ibdev)->dev, pd->pdn);
			kfree(pd);
			return ERR_PTR(-EFAULT);
		}

	return &pd->ibpd;
}

static int mlx4_ib_dealloc_pd(struct ib_pd *pd)
{
	mlx4_pd_free(to_mdev(pd->device)->dev, to_mpd(pd)->pdn);
	kfree(pd);

	return 0;
}

static struct ib_xrcd *mlx4_ib_alloc_xrcd(struct ib_device *ibdev,
					  struct ib_ucontext *context,
					  struct ib_udata *udata)
{
	struct mlx4_ib_xrcd *xrcd;
	int err;

	if (!(to_mdev(ibdev)->dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC))
		return ERR_PTR(-ENOSYS);

	xrcd = kmalloc(sizeof *xrcd, GFP_KERNEL);
	if (!xrcd)
		return ERR_PTR(-ENOMEM);

	err = mlx4_xrcd_alloc(to_mdev(ibdev)->dev, &xrcd->xrcdn);
	if (err)
		goto err1;

	xrcd->pd = ib_alloc_pd(ibdev);
	if (IS_ERR(xrcd->pd)) {
		err = PTR_ERR(xrcd->pd);
		goto err2;
	}

	xrcd->cq = ib_create_cq(ibdev, NULL, NULL, xrcd, 1, 0);
	if (IS_ERR(xrcd->cq)) {
		err = PTR_ERR(xrcd->cq);
		goto err3;
	}

	return &xrcd->ibxrcd;

err3:
	ib_dealloc_pd(xrcd->pd);
err2:
	mlx4_xrcd_free(to_mdev(ibdev)->dev, xrcd->xrcdn);
err1:
	kfree(xrcd);
	return ERR_PTR(err);
}

static int mlx4_ib_dealloc_xrcd(struct ib_xrcd *xrcd)
{
	ib_destroy_cq(to_mxrcd(xrcd)->cq);
	ib_dealloc_pd(to_mxrcd(xrcd)->pd);
	mlx4_xrcd_free(to_mdev(xrcd->device)->dev, to_mxrcd(xrcd)->xrcdn);
	kfree(xrcd);

	return 0;
}

static int add_gid_entry(struct ib_qp *ibqp, union ib_gid *gid)
{
	struct mlx4_ib_qp *mqp = to_mqp(ibqp);
	struct mlx4_ib_dev *mdev = to_mdev(ibqp->device);
	struct mlx4_ib_gid_entry *ge;

	ge = kzalloc(sizeof *ge, GFP_KERNEL);
	if (!ge)
		return -ENOMEM;

	ge->gid = *gid;
	if (mlx4_ib_add_mc(mdev, mqp, gid)) {
		ge->port = mqp->port;
		ge->added = 1;
	}

	mutex_lock(&mqp->mutex);
	list_add_tail(&ge->list, &mqp->gid_list);
	mutex_unlock(&mqp->mutex);

	return 0;
}

int mlx4_ib_add_mc(struct mlx4_ib_dev *mdev, struct mlx4_ib_qp *mqp,
		   union ib_gid *gid)
{
	struct net_device *ndev;
	int ret = 0;

	if (!mqp->port)
		return 0;

	spin_lock_bh(&mdev->iboe.lock);
	ndev = mdev->iboe.netdevs[mqp->port - 1];
	if (ndev)
		dev_hold(ndev);
	spin_unlock_bh(&mdev->iboe.lock);

	if (ndev) {
		ret = 1;
		dev_put(ndev);
	}

	return ret;
}

struct mlx4_ib_steering {
	struct list_head list;
	u64 reg_id;
	union ib_gid gid;
};

static int parse_flow_attr(struct mlx4_dev *dev,
			   u32 qp_num,
			   union ib_flow_spec *ib_spec,
			   struct _rule_hw *mlx4_spec)
{
	enum mlx4_net_trans_rule_id type;

	switch (ib_spec->type) {
	case IB_FLOW_SPEC_ETH:
		type = MLX4_NET_TRANS_RULE_ID_ETH;
		memcpy(mlx4_spec->eth.dst_mac, ib_spec->eth.val.dst_mac,
		       ETH_ALEN);
		memcpy(mlx4_spec->eth.dst_mac_msk, ib_spec->eth.mask.dst_mac,
		       ETH_ALEN);
		mlx4_spec->eth.vlan_tag = ib_spec->eth.val.vlan_tag;
		mlx4_spec->eth.vlan_tag_msk = ib_spec->eth.mask.vlan_tag;
		break;
	case IB_FLOW_SPEC_IB:
		type = MLX4_NET_TRANS_RULE_ID_IB;
		mlx4_spec->ib.l3_qpn =
			cpu_to_be32(qp_num);
		mlx4_spec->ib.qpn_mask =
			cpu_to_be32(MLX4_IB_FLOW_QPN_MASK);
		break;


	case IB_FLOW_SPEC_IPV4:
		type = MLX4_NET_TRANS_RULE_ID_IPV4;
		mlx4_spec->ipv4.src_ip = ib_spec->ipv4.val.src_ip;
		mlx4_spec->ipv4.src_ip_msk = ib_spec->ipv4.mask.src_ip;
		mlx4_spec->ipv4.dst_ip = ib_spec->ipv4.val.dst_ip;
		mlx4_spec->ipv4.dst_ip_msk = ib_spec->ipv4.mask.dst_ip;
		break;

	case IB_FLOW_SPEC_TCP:
	case IB_FLOW_SPEC_UDP:
		type = ib_spec->type == IB_FLOW_SPEC_TCP ?
					MLX4_NET_TRANS_RULE_ID_TCP :
					MLX4_NET_TRANS_RULE_ID_UDP;
		mlx4_spec->tcp_udp.dst_port = ib_spec->tcp_udp.val.dst_port;
		mlx4_spec->tcp_udp.dst_port_msk = ib_spec->tcp_udp.mask.dst_port;
		mlx4_spec->tcp_udp.src_port = ib_spec->tcp_udp.val.src_port;
		mlx4_spec->tcp_udp.src_port_msk = ib_spec->tcp_udp.mask.src_port;
		break;

	default:
		return -EINVAL;
	}
	if (mlx4_map_sw_to_hw_steering_id(dev, type) < 0 ||
	    mlx4_hw_rule_sz(dev, type) < 0)
		return -EINVAL;
	mlx4_spec->id = cpu_to_be16(mlx4_map_sw_to_hw_steering_id(dev, type));
	mlx4_spec->size = mlx4_hw_rule_sz(dev, type) >> 2;
	return mlx4_hw_rule_sz(dev, type);
}

struct default_rules {
	__u32 mandatory_fields[IB_FLOW_SPEC_SUPPORT_LAYERS];
	__u32 mandatory_not_fields[IB_FLOW_SPEC_SUPPORT_LAYERS];
	__u32 rules_create_list[IB_FLOW_SPEC_SUPPORT_LAYERS];
	__u8  link_layer;
};
static const struct default_rules default_table[] = {
	{
		.mandatory_fields = {IB_FLOW_SPEC_IPV4},
		.mandatory_not_fields = {IB_FLOW_SPEC_ETH},
		.rules_create_list = {IB_FLOW_SPEC_IB},
		.link_layer = IB_LINK_LAYER_INFINIBAND
	}
};

static int __mlx4_ib_default_rules_match(struct ib_qp *qp,
					 struct ib_flow_attr *flow_attr)
{
	int i, j, k;
	void *ib_flow;
	const struct default_rules *pdefault_rules = default_table;
	u8 link_layer = rdma_port_get_link_layer(qp->device, flow_attr->port);

	for (i = 0; i < ARRAY_SIZE(default_table); i++, pdefault_rules++) {
		__u32 field_types[IB_FLOW_SPEC_SUPPORT_LAYERS];
		memset(&field_types, 0, sizeof(field_types));

		if (link_layer != pdefault_rules->link_layer)
			continue;

		ib_flow = flow_attr + 1;
		/* we assume the specs are sorted */
		for (j = 0, k = 0; k < IB_FLOW_SPEC_SUPPORT_LAYERS &&
		     j < flow_attr->num_of_specs; k++) {
			union ib_flow_spec *current_flow =
				(union ib_flow_spec *)ib_flow;

			/* same layer but different type */
			if (((current_flow->type & IB_FLOW_SPEC_LAYER_MASK) ==
			     (pdefault_rules->mandatory_fields[k] &
			      IB_FLOW_SPEC_LAYER_MASK)) &&
			    (current_flow->type !=
			     pdefault_rules->mandatory_fields[k]))
				goto out;

			/* same layer, try match next one */
			if (current_flow->type ==
			    pdefault_rules->mandatory_fields[k]) {
				j++;
				ib_flow +=
					((union ib_flow_spec *)ib_flow)->size;
			}
		}

		ib_flow = flow_attr + 1;
		for (j = 0; j < flow_attr->num_of_specs;
		     j++, ib_flow += ((union ib_flow_spec *)ib_flow)->size)
			for (k = 0; k < IB_FLOW_SPEC_SUPPORT_LAYERS; k++)
				/* same layer and same type */
				if (((union ib_flow_spec *)ib_flow)->type ==
				    pdefault_rules->mandatory_not_fields[k])
					goto out;

		return i;
	}
out:
	return -1;
}

static int __mlx4_ib_create_default_rules(
		struct mlx4_ib_dev *mdev,
		struct ib_qp *qp,
		const struct default_rules *pdefault_rules,
		struct _rule_hw *mlx4_spec) {
	int size = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(pdefault_rules->rules_create_list); i++) {
		int ret;
		union ib_flow_spec ib_spec;
		switch (pdefault_rules->rules_create_list[i]) {
		case 0:
			/* no rule */
			continue;
		case IB_FLOW_SPEC_IB:
			ib_spec.type = IB_FLOW_SPEC_IB;
			ib_spec.size = sizeof(struct ib_flow_spec_ib);

			break;
		default:
			/* invalid rule */
			return -EINVAL;
		}
		/* We must put empty rule, qpn is being ignored */
		ret = parse_flow_attr(mdev->dev, 0, &ib_spec,
				      mlx4_spec);
		if (ret < 0) {
			pr_info("invalid parsing\n");
			return -EINVAL;
		}

		mlx4_spec = (void *)mlx4_spec + ret;
		size += ret;
	}
	return size;
}

static int __mlx4_ib_create_flow(struct ib_qp *qp, struct ib_flow_attr *flow_attr,
			  int domain,
			  enum mlx4_net_trans_promisc_mode flow_type,
			  u64 *reg_id)
{
	int ret, i;
	int size = 0;
	void *ib_flow;
	struct mlx4_ib_dev *mdev = to_mdev(qp->device);
	struct mlx4_cmd_mailbox *mailbox;
	struct mlx4_net_trans_rule_hw_ctrl *ctrl;
	int default_flow;

	static const u16 __mlx4_domain[] = {
		[IB_FLOW_DOMAIN_USER] = MLX4_DOMAIN_UVERBS,
		[IB_FLOW_DOMAIN_ETHTOOL] = MLX4_DOMAIN_ETHTOOL,
		[IB_FLOW_DOMAIN_RFS] = MLX4_DOMAIN_RFS,
		[IB_FLOW_DOMAIN_NIC] = MLX4_DOMAIN_NIC,
	};

	if (flow_attr->priority > MLX4_IB_FLOW_MAX_PRIO) {
		pr_err("Invalid priority value %d\n", flow_attr->priority);
		return -EINVAL;
	}

	if (domain >= IB_FLOW_DOMAIN_NUM) {
		pr_err("Invalid domain value %d\n", domain);
		return -EINVAL;
	}

	if (mlx4_map_sw_to_hw_steering_mode(mdev->dev, flow_type) < 0)
		return -EINVAL;

	mailbox = mlx4_alloc_cmd_mailbox(mdev->dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	ctrl = mailbox->buf;

	ctrl->prio = cpu_to_be16(__mlx4_domain[domain] |
				 flow_attr->priority);
	ctrl->type = mlx4_map_sw_to_hw_steering_mode(mdev->dev, flow_type);
	ctrl->port = flow_attr->port;
	ctrl->qpn = cpu_to_be32(qp->qp_num);

	ib_flow = flow_attr + 1;
	size += sizeof(struct mlx4_net_trans_rule_hw_ctrl);
	/* Add default flows */
	default_flow = __mlx4_ib_default_rules_match(qp, flow_attr);
	if (default_flow >= 0) {
		ret = __mlx4_ib_create_default_rules(
				mdev, qp, default_table + default_flow,
				mailbox->buf + size);
		if (ret < 0) {
			mlx4_free_cmd_mailbox(mdev->dev, mailbox);
			return -EINVAL;
		}
		size += ret;
	}
	for (i = 0; i < flow_attr->num_of_specs; i++) {
		ret = parse_flow_attr(mdev->dev, qp->qp_num, ib_flow,
				      mailbox->buf + size);
		if (ret < 0) {
			mlx4_free_cmd_mailbox(mdev->dev, mailbox);
			return -EINVAL;
		}
		ib_flow += ((union ib_flow_spec *) ib_flow)->size;
		size += ret;
	}

	ret = mlx4_cmd_imm(mdev->dev, mailbox->dma, reg_id, size >> 2, 0,
			   MLX4_QP_FLOW_STEERING_ATTACH, MLX4_CMD_TIME_CLASS_A,
			   MLX4_CMD_NATIVE);
	if (ret == -ENOMEM)
		pr_err("mcg table is full. Fail to register network rule.\n");
	else if (ret == -ENXIO)
		pr_err("Device managed flow steering is disabled. Fail to register network rule.\n");
	else if (ret)
		pr_err("Invalid argumant. Fail to register network rule.\n");

	mlx4_free_cmd_mailbox(mdev->dev, mailbox);
	return ret;
}

static int __mlx4_ib_destroy_flow(struct mlx4_dev *dev, u64 reg_id)
{
	int err;
	err = mlx4_cmd(dev, reg_id, 0, 0,
		       MLX4_QP_FLOW_STEERING_DETACH, MLX4_CMD_TIME_CLASS_A,
		       MLX4_CMD_NATIVE);
	if (err)
		pr_err("Fail to detach network rule. registration id = 0x%llx\n",
		       reg_id);
	return err;
}

static struct ib_flow *mlx4_ib_create_flow(struct ib_qp *qp,
				    struct ib_flow_attr *flow_attr,
				    int domain)
{
	int err = 0, i = 0;
	struct mlx4_ib_flow *mflow;
	enum mlx4_net_trans_promisc_mode type[2];

	memset(type, 0, sizeof(type));

	mflow = kzalloc(sizeof(*mflow), GFP_KERNEL);
	if (!mflow) {
		err = -ENOMEM;
		goto err_free;
	}

	switch (flow_attr->type) {
	case IB_FLOW_ATTR_NORMAL:
		type[0] = MLX4_FS_REGULAR;
		break;

	case IB_FLOW_ATTR_ALL_DEFAULT:
		type[0] = MLX4_FS_ALL_DEFAULT;
		break;

	case IB_FLOW_ATTR_MC_DEFAULT:
		type[0] = MLX4_FS_MC_DEFAULT;
		break;

	case IB_FLOW_ATTR_SNIFFER:
		type[0] = MLX4_FS_UC_SNIFFER;
		type[1] = MLX4_FS_MC_SNIFFER;
		break;

	default:
		err = -EINVAL;
		goto err_free;
	}

	while (i < ARRAY_SIZE(type) && type[i]) {
		err = __mlx4_ib_create_flow(qp, flow_attr, domain, type[i],
					    &mflow->reg_id[i]);
		if (err)
			goto err_free;
		i++;
	}

	return &mflow->ibflow;

err_free:
	kfree(mflow);
	return ERR_PTR(err);
}

static int mlx4_ib_destroy_flow(struct ib_flow *flow_id)
{
	int err, ret = 0;
	int i = 0;
	struct mlx4_ib_dev *mdev = to_mdev(flow_id->qp->device);
	struct mlx4_ib_flow *mflow = to_mflow(flow_id);

	while (i < ARRAY_SIZE(mflow->reg_id) && mflow->reg_id[i]) {
		err = __mlx4_ib_destroy_flow(mdev->dev, mflow->reg_id[i]);
		if (err)
			ret = err;
		i++;
	}

	kfree(mflow);
	return ret;
}

static int mlx4_ib_mcg_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	int err;
	struct mlx4_ib_dev *mdev = to_mdev(ibqp->device);
	struct mlx4_ib_qp *mqp = to_mqp(ibqp);
	u64 reg_id;
	struct mlx4_ib_steering *ib_steering = NULL;
	enum mlx4_protocol prot = (gid->raw[1] == 0x0e) ?
		MLX4_PROT_IB_IPV4 : MLX4_PROT_IB_IPV6;

	if (mdev->dev->caps.steering_mode ==
	    MLX4_STEERING_MODE_DEVICE_MANAGED) {
		ib_steering = kmalloc(sizeof(*ib_steering), GFP_KERNEL);
		if (!ib_steering)
			return -ENOMEM;
	}

	err = mlx4_multicast_attach(mdev->dev, &mqp->mqp, gid->raw, mqp->port,
				    !!(mqp->flags &
				       MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK),
				    prot, &reg_id);
	if (err)
		goto err_malloc;

	err = add_gid_entry(ibqp, gid);
	if (err)
		goto err_add;

	if (ib_steering) {
		memcpy(ib_steering->gid.raw, gid->raw, 16);
		ib_steering->reg_id = reg_id;
		mutex_lock(&mqp->mutex);
		list_add(&ib_steering->list, &mqp->steering_rules);
		mutex_unlock(&mqp->mutex);
	}
	return 0;

err_add:
	mlx4_multicast_detach(mdev->dev, &mqp->mqp, gid->raw,
			      prot, reg_id);
err_malloc:
	kfree(ib_steering);

	return err;
}

static struct mlx4_ib_gid_entry *find_gid_entry(struct mlx4_ib_qp *qp, u8 *raw)
{
	struct mlx4_ib_gid_entry *ge;
	struct mlx4_ib_gid_entry *tmp;
	struct mlx4_ib_gid_entry *ret = NULL;

	list_for_each_entry_safe(ge, tmp, &qp->gid_list, list) {
		if (!memcmp(raw, ge->gid.raw, 16)) {
			ret = ge;
			break;
		}
	}

	return ret;
}

static int mlx4_ib_mcg_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	int err;
	struct mlx4_ib_dev *mdev = to_mdev(ibqp->device);
	struct mlx4_ib_qp *mqp = to_mqp(ibqp);
	struct net_device *ndev;
	struct mlx4_ib_gid_entry *ge;
	u64 reg_id = 0;
	enum mlx4_protocol prot = (gid->raw[1] == 0x0e) ?
		MLX4_PROT_IB_IPV4 : MLX4_PROT_IB_IPV6;

	if (mdev->dev->caps.steering_mode ==
	    MLX4_STEERING_MODE_DEVICE_MANAGED) {
		struct mlx4_ib_steering *ib_steering;

		mutex_lock(&mqp->mutex);
		list_for_each_entry(ib_steering, &mqp->steering_rules, list) {
			if (!memcmp(ib_steering->gid.raw, gid->raw, 16)) {
				list_del(&ib_steering->list);
				break;
			}
		}
		mutex_unlock(&mqp->mutex);
		if (&ib_steering->list == &mqp->steering_rules) {
			pr_err("Couldn't find reg_id for mgid. Steering rule is left attached\n");
			return -EINVAL;
		}
		reg_id = ib_steering->reg_id;
		kfree(ib_steering);
	}

	err = mlx4_multicast_detach(mdev->dev, &mqp->mqp, gid->raw,
				    prot, reg_id);
	if (err)
		return err;

	mutex_lock(&mqp->mutex);
	ge = find_gid_entry(mqp, gid->raw);
	if (ge) {
		spin_lock_bh(&mdev->iboe.lock);
		ndev = ge->added ? mdev->iboe.netdevs[ge->port - 1] : NULL;
		if (ndev)
			dev_hold(ndev);
		spin_unlock_bh(&mdev->iboe.lock);
		if (ndev)
			dev_put(ndev);
		list_del(&ge->list);
		kfree(ge);
	} else
		pr_warn("could not find mgid entry\n");

	mutex_unlock(&mqp->mutex);

	return 0;
}

static int init_node_data(struct mlx4_ib_dev *dev)
{
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int mad_ifc_flags = MLX4_MAD_IFC_IGNORE_KEYS;
	int err = -ENOMEM;

	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id = IB_SMP_ATTR_NODE_DESC;
	if (mlx4_is_master(dev->dev))
		mad_ifc_flags |= MLX4_MAD_IFC_NET_VIEW;

	err = mlx4_MAD_IFC(dev, mad_ifc_flags, 1, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	memcpy(dev->ib_dev.node_desc, out_mad->data, 64);

	in_mad->attr_id = IB_SMP_ATTR_NODE_INFO;

	err = mlx4_MAD_IFC(dev, mad_ifc_flags, 1, NULL, NULL, in_mad, out_mad);
	if (err)
		goto out;

	dev->dev->rev_id = be32_to_cpup((__be32 *) (out_mad->data + 32));
	memcpy(&dev->ib_dev.node_guid, out_mad->data + 12, 8);

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static ssize_t show_hca(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct mlx4_ib_dev *dev =
		container_of(device, struct mlx4_ib_dev, ib_dev.dev);
	return sprintf(buf, "MT%d\n", dev->dev->pdev->device);
}

static ssize_t show_fw_ver(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	struct mlx4_ib_dev *dev =
		container_of(device, struct mlx4_ib_dev, ib_dev.dev);
	return sprintf(buf, "%d.%d.%d\n", (int) (dev->dev->caps.fw_ver >> 32),
		       (int) (dev->dev->caps.fw_ver >> 16) & 0xffff,
		       (int) dev->dev->caps.fw_ver & 0xffff);
}

static ssize_t show_rev(struct device *device, struct device_attribute *attr,
			char *buf)
{
	struct mlx4_ib_dev *dev =
		container_of(device, struct mlx4_ib_dev, ib_dev.dev);
	return sprintf(buf, "%x\n", dev->dev->rev_id);
}

static ssize_t show_board(struct device *device, struct device_attribute *attr,
			  char *buf)
{
	struct mlx4_ib_dev *dev =
		container_of(device, struct mlx4_ib_dev, ib_dev.dev);
	return sprintf(buf, "%.*s\n", MLX4_BOARD_ID_LEN,
		       dev->dev->board_id);
}

static DEVICE_ATTR(hw_rev,   S_IRUGO, show_rev,    NULL);
static DEVICE_ATTR(fw_ver,   S_IRUGO, show_fw_ver, NULL);
static DEVICE_ATTR(hca_type, S_IRUGO, show_hca,    NULL);
static DEVICE_ATTR(board_id, S_IRUGO, show_board,  NULL);

static struct device_attribute *mlx4_class_attributes[] = {
	&dev_attr_hw_rev,
	&dev_attr_fw_ver,
	&dev_attr_hca_type,
	&dev_attr_board_id
};

static void mlx4_addrconf_ifid_eui48(u8 *eui, u16 vlan_id,
				     struct net_device *dev)
{
	memcpy(eui, dev->dev_addr, 3);
	memcpy(eui + 5, dev->dev_addr + 3, 3);
	if (vlan_id < 0x1000) {
		eui[3] = vlan_id >> 8;
		eui[4] = vlan_id & 0xff;
	} else {
		eui[3] = 0xff;
		eui[4] = 0xfe;
	}
	eui[0] ^= 2;
}

static void update_gids_task(struct work_struct *work)
{
	struct update_gid_work *gw = container_of(work, struct update_gid_work, work);
	struct mlx4_cmd_mailbox *mailbox;
	union ib_gid *gids;
	int err;
	struct mlx4_dev	*dev = gw->dev->dev;

	if (!gw->dev->ib_active)
		return;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		pr_warn("update gid table failed %ld\n", PTR_ERR(mailbox));
		return;
	}

	gids = mailbox->buf;
	memcpy(gids, gw->gids, sizeof gw->gids);

	err = mlx4_cmd(dev, mailbox->dma, MLX4_SET_PORT_GID_TABLE << 8 | gw->port,
		       1, MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_WRAPPED);
	if (err)
		pr_warn("set port command failed\n");
	else
		mlx4_ib_dispatch_event(gw->dev, gw->port, IB_EVENT_GID_CHANGE);

	mlx4_free_cmd_mailbox(dev, mailbox);
	kfree(gw);
}

static void reset_gids_task(struct work_struct *work)
{
	struct update_gid_work *gw =
			container_of(work, struct update_gid_work, work);
	struct mlx4_cmd_mailbox *mailbox;
	union ib_gid *gids;
	int err;
	struct mlx4_dev	*dev = gw->dev->dev;

	if (!gw->dev->ib_active)
		return;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox)) {
		pr_warn("reset gid table failed\n");
		goto free;
	}

	gids = mailbox->buf;
	memcpy(gids, gw->gids, sizeof(gw->gids));

	if (mlx4_ib_port_link_layer(&gw->dev->ib_dev, gw->port) ==
				    IB_LINK_LAYER_ETHERNET) {
		err = mlx4_cmd(dev, mailbox->dma,
			       MLX4_SET_PORT_GID_TABLE << 8 | gw->port,
			       1, MLX4_CMD_SET_PORT,
			       MLX4_CMD_TIME_CLASS_B,
			       MLX4_CMD_WRAPPED);
		if (err)
			pr_warn(KERN_WARNING
				"set port %d command failed\n", gw->port);
	}

	mlx4_free_cmd_mailbox(dev, mailbox);
free:
	kfree(gw);
}

static int update_gid_table(struct mlx4_ib_dev *dev, int port,
			    union ib_gid *gid, int clear,
			    int default_gid)
{
	struct update_gid_work *work;
	int i;
	int need_update = 0;
	int free = -1;
	int found = -1;
	int max_gids;

	if (default_gid) {
		free = 0;
	} else {
		max_gids = dev->dev->caps.gid_table_len[port];
		for (i = 1; i < max_gids; ++i) {
			if (!memcmp(&dev->iboe.gid_table[port - 1][i], gid,
				    sizeof(*gid)))
				found = i;

			if (clear) {
				if (found >= 0) {
					need_update = 1;
					dev->iboe.gid_table[port - 1][found] =
						zgid;
					break;
				}
			} else {
				if (found >= 0)
					break;

				if (free < 0 &&
				    !memcmp(&dev->iboe.gid_table[port - 1][i],
					    &zgid, sizeof(*gid)))
					free = i;
			}
		}
	}

	if (found == -1 && !clear && free >= 0) {
		dev->iboe.gid_table[port - 1][free] = *gid;
		need_update = 1;
	}

	if (!need_update)
		return 0;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return -ENOMEM;

	memcpy(work->gids, dev->iboe.gid_table[port - 1], sizeof(work->gids));
	INIT_WORK(&work->work, update_gids_task);
	work->port = port;
	work->dev = dev;
	queue_work(wq, &work->work);

	return 0;
}

static void mlx4_make_default_gid(struct  net_device *dev, union ib_gid *gid)
{
	gid->global.subnet_prefix = cpu_to_be64(0xfe80000000000000LL);
	mlx4_addrconf_ifid_eui48(&gid->raw[8], 0xffff, dev);
}


static int reset_gid_table(struct mlx4_ib_dev *dev, u8 port)
{
	struct update_gid_work *work;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return -ENOMEM;

	memset(dev->iboe.gid_table[port - 1], 0, sizeof(work->gids));
	memset(work->gids, 0, sizeof(work->gids));
	INIT_WORK(&work->work, reset_gids_task);
	work->dev = dev;
	work->port = port;
	queue_work(wq, &work->work);
	return 0;
}

static int mlx4_ib_addr_event(int event, struct net_device *event_netdev,
			      struct mlx4_ib_dev *ibdev, union ib_gid *gid)
{
	struct mlx4_ib_iboe *iboe;
	int port = 0;
	struct net_device *real_dev = rdma_vlan_dev_real_dev(event_netdev) ?
				rdma_vlan_dev_real_dev(event_netdev) :
				event_netdev;
	union ib_gid default_gid;

	mlx4_make_default_gid(real_dev, &default_gid);

	if (!memcmp(gid, &default_gid, sizeof(*gid)))
		return 0;

	if (event != NETDEV_DOWN && event != NETDEV_UP)
		return 0;

	if ((real_dev != event_netdev) &&
	    (event == NETDEV_DOWN) &&
	    rdma_link_local_addr((struct in6_addr *)gid))
		return 0;

	iboe = &ibdev->iboe;
	spin_lock_bh(&iboe->lock);

	for (port = 1; port <= ibdev->dev->caps.num_ports; ++port)
		if ((netif_is_bond_master(real_dev) &&
		     (real_dev == iboe->masters[port - 1])) ||
		     (!netif_is_bond_master(real_dev) &&
		     (real_dev == iboe->netdevs[port - 1])))
			update_gid_table(ibdev, port, gid,
					 event == NETDEV_DOWN, 0);

	spin_unlock_bh(&iboe->lock);
	return 0;

}

static u8 mlx4_ib_get_dev_port(struct net_device *dev,
			       struct mlx4_ib_dev *ibdev)
{
	u8 port = 0;
	struct mlx4_ib_iboe *iboe;
	struct net_device *real_dev = rdma_vlan_dev_real_dev(dev) ?
				rdma_vlan_dev_real_dev(dev) : dev;

	iboe = &ibdev->iboe;

	for (port = 1; port <= ibdev->dev->caps.num_ports; ++port)
		if ((netif_is_bond_master(real_dev) &&
		     (real_dev == iboe->masters[port - 1])) ||
		     (!netif_is_bond_master(real_dev) &&
		     (real_dev == iboe->netdevs[port - 1])))
			break;

	if ((port == 0) || (port > ibdev->dev->caps.num_ports))
		return 0;
	else
		return port;
}

static int mlx4_ib_inet_event(struct notifier_block *this, unsigned long event,
				void *ptr)
{
	struct mlx4_ib_dev *ibdev;
	struct in_ifaddr *ifa = ptr;
	union ib_gid gid;
	struct net_device *event_netdev = ifa->ifa_dev->dev;

	ipv6_addr_set_v4mapped(ifa->ifa_address, (struct in6_addr *)&gid);

	ibdev = container_of(this, struct mlx4_ib_dev, iboe.nb_inet);

	mlx4_ib_addr_event(event, event_netdev, ibdev, &gid);
	return NOTIFY_DONE;
}

#if IS_ENABLED(CONFIG_IPV6)
static int mlx4_ib_inet6_event(struct notifier_block *this, unsigned long event,
				void *ptr)
{
	struct mlx4_ib_dev *ibdev;
	struct inet6_ifaddr *ifa = ptr;
	union  ib_gid *gid = (union ib_gid *)&ifa->addr;
	struct net_device *event_netdev = ifa->idev->dev;

	ibdev = container_of(this, struct mlx4_ib_dev, iboe.nb_inet6);

	mlx4_ib_addr_event(event, event_netdev, ibdev, gid);
	return NOTIFY_DONE;
}
#endif

#define MLX4_IB_INVALID_MAC	((u64)-1)
static void mlx4_ib_update_qps(struct mlx4_ib_dev *ibdev,
			       struct net_device *dev,
			       int port)
{
	u64 new_smac = 0;
	u64 release_mac = MLX4_IB_INVALID_MAC;
	struct mlx4_ib_qp *qp;

	read_lock(&dev_base_lock);
	new_smac = mlx4_mac_to_u64(dev->dev_addr);
	read_unlock(&dev_base_lock);

	atomic64_set(&ibdev->iboe.mac[port - 1], new_smac);

	/* no need for update QP1 and mac registration in non-SRIOV */
	if (!mlx4_is_mfunc(ibdev->dev))
		return;

	mutex_lock(&ibdev->qp1_proxy_lock[port - 1]);
	qp = ibdev->qp1_proxy[port - 1];
	if (qp) {
		int new_smac_index;
		u64 old_smac;
		struct mlx4_update_qp_params update_params;

		mutex_lock(&qp->mutex);
		old_smac = qp->pri.smac;
		if (new_smac == old_smac)
			goto unlock;

		new_smac_index = mlx4_register_mac(ibdev->dev, port, new_smac);

		if (new_smac_index < 0)
			goto unlock;

		update_params.smac_index = new_smac_index;
		if (mlx4_update_qp(ibdev->dev, &qp->mqp, MLX4_UPDATE_QP_SMAC,
				   &update_params)) {
			release_mac = new_smac;
			goto unlock;
		}
		/* if old port was zero, no mac was yet registered for this QP */
		if (qp->pri.smac_port)
			release_mac = old_smac;
		qp->pri.smac = new_smac;
		qp->pri.smac_port = port;
		qp->pri.smac_index = new_smac_index;
	}

unlock:
	if (release_mac != MLX4_IB_INVALID_MAC)
		mlx4_unregister_mac(ibdev->dev, port, release_mac);
	if (qp)
		mutex_unlock(&qp->mutex);
	mutex_unlock(&ibdev->qp1_proxy_lock[port - 1]);
}

static void mlx4_ib_get_dev_addr(struct net_device *dev,
				 struct mlx4_ib_dev *ibdev, u8 port)
{
	struct in_device *in_dev;
#if IS_ENABLED(CONFIG_IPV6)
	struct inet6_dev *in6_dev;
	union ib_gid  *pgid;
	struct inet6_ifaddr *ifp;
	union ib_gid default_gid;
#endif
	union ib_gid gid;


	if ((port == 0) || (port > ibdev->dev->caps.num_ports))
		return;

	/* IPv4 gids */
	in_dev = in_dev_get(dev);
	if (in_dev) {
		for_ifa(in_dev) {
			/*ifa->ifa_address;*/
			ipv6_addr_set_v4mapped(ifa->ifa_address,
					       (struct in6_addr *)&gid);
			update_gid_table(ibdev, port, &gid, 0, 0);
		}
		endfor_ifa(in_dev);
		in_dev_put(in_dev);
	}
#if IS_ENABLED(CONFIG_IPV6)
	mlx4_make_default_gid(dev, &default_gid);
	/* IPv6 gids */
	in6_dev = in6_dev_get(dev);
	if (in6_dev) {
		read_lock_bh(&in6_dev->lock);
		list_for_each_entry(ifp, &in6_dev->addr_list, if_list) {
			pgid = (union ib_gid *)&ifp->addr;
			if (!memcmp(pgid, &default_gid, sizeof(*pgid)))
				continue;
			update_gid_table(ibdev, port, pgid, 0, 0);
		}
		read_unlock_bh(&in6_dev->lock);
		in6_dev_put(in6_dev);
	}
#endif
}

static void mlx4_ib_set_default_gid(struct mlx4_ib_dev *ibdev,
				 struct  net_device *dev, u8 port)
{
	union ib_gid gid;
	mlx4_make_default_gid(dev, &gid);
	update_gid_table(ibdev, port, &gid, 0, 1);
}

static int mlx4_ib_init_gid_table(struct mlx4_ib_dev *ibdev)
{
	struct	net_device *dev;
	struct mlx4_ib_iboe *iboe = &ibdev->iboe;
	int i;
	int err = 0;

	for (i = 1; i <= ibdev->num_ports; ++i) {
		if (rdma_port_get_link_layer(&ibdev->ib_dev, i) ==
		    IB_LINK_LAYER_ETHERNET) {
			err = reset_gid_table(ibdev, i);
			if (err)
				goto out;
		}
	}

	read_lock(&dev_base_lock);
	spin_lock_bh(&iboe->lock);

	for_each_netdev(&init_net, dev) {
		u8 port = mlx4_ib_get_dev_port(dev, ibdev);
		/* port will be non-zero only for ETH ports */
		if (port) {
			mlx4_ib_set_default_gid(ibdev, dev, port);
			mlx4_ib_get_dev_addr(dev, ibdev, port);
		}
	}

	spin_unlock_bh(&iboe->lock);
	read_unlock(&dev_base_lock);
out:
	return err;
}

static void mlx4_ib_scan_netdevs(struct mlx4_ib_dev *ibdev,
				 struct net_device *dev,
				 unsigned long event)

{
	struct mlx4_ib_iboe *iboe;
	int update_qps_port = -1;
	int port;

	iboe = &ibdev->iboe;

	spin_lock_bh(&iboe->lock);
	mlx4_foreach_ib_transport_port(port, ibdev->dev) {
		enum ib_port_state	port_state = IB_PORT_NOP;
		struct net_device *old_master = iboe->masters[port - 1];
		struct net_device *curr_netdev;
		struct net_device *curr_master;

		iboe->netdevs[port - 1] =
			mlx4_get_protocol_dev(ibdev->dev, MLX4_PROT_ETH, port);
		if (iboe->netdevs[port - 1])
			mlx4_ib_set_default_gid(ibdev,
						iboe->netdevs[port - 1], port);
		curr_netdev = iboe->netdevs[port - 1];

		if (iboe->netdevs[port - 1] &&
		    netif_is_bond_slave(iboe->netdevs[port - 1])) {
			iboe->masters[port - 1] = netdev_master_upper_dev_get(
				iboe->netdevs[port - 1]);
		} else {
			iboe->masters[port - 1] = NULL;
		}
		curr_master = iboe->masters[port - 1];

		if (dev == iboe->netdevs[port - 1] &&
		    (event == NETDEV_CHANGEADDR || event == NETDEV_REGISTER ||
		     event == NETDEV_UP || event == NETDEV_CHANGE))
			update_qps_port = port;

		if (curr_netdev) {
			port_state = (netif_running(curr_netdev) && netif_carrier_ok(curr_netdev)) ?
						IB_PORT_ACTIVE : IB_PORT_DOWN;
			mlx4_ib_set_default_gid(ibdev, curr_netdev, port);
			if (curr_master) {
				/* if using bonding/team and a slave port is down, we
				 * don't want the bond IP based gids in the table since
				 * flows that select port by gid may get the down port.
				*/
				if (port_state == IB_PORT_DOWN) {
					reset_gid_table(ibdev, port);
					mlx4_ib_set_default_gid(ibdev,
								curr_netdev,
								port);
				} else {
					/* gids from the upper dev (bond/team)
					 * should appear in port's gid table
					*/
					mlx4_ib_get_dev_addr(curr_master,
							     ibdev, port);
				}
			}
			/* if bonding is used it is possible that we add it to
			 * masters only after IP address is assigned to the
			 * net bonding interface.
			*/
			if (curr_master && (old_master != curr_master)) {
				reset_gid_table(ibdev, port);
				mlx4_ib_set_default_gid(ibdev,
							curr_netdev, port);
				mlx4_ib_get_dev_addr(curr_master, ibdev, port);
			}

			if (!curr_master && (old_master != curr_master)) {
				reset_gid_table(ibdev, port);
				mlx4_ib_set_default_gid(ibdev,
							curr_netdev, port);
				mlx4_ib_get_dev_addr(curr_netdev, ibdev, port);
			}
		} else {
			reset_gid_table(ibdev, port);
		}
	}

	spin_unlock_bh(&iboe->lock);

	if (update_qps_port > 0)
		mlx4_ib_update_qps(ibdev, dev, update_qps_port);
}

static int mlx4_ib_netdev_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct mlx4_ib_dev *ibdev;

	if (!net_eq(dev_net(dev), &init_net))
		return NOTIFY_DONE;

	ibdev = container_of(this, struct mlx4_ib_dev, iboe.nb);
	mlx4_ib_scan_netdevs(ibdev, dev, event);

	return NOTIFY_DONE;
}

static void init_pkeys(struct mlx4_ib_dev *ibdev)
{
	int port;
	int slave;
	int i;

	if (mlx4_is_master(ibdev->dev)) {
		for (slave = 0; slave <= ibdev->dev->num_vfs; ++slave) {
			for (port = 1; port <= ibdev->dev->caps.num_ports; ++port) {
				for (i = 0;
				     i < ibdev->dev->phys_caps.pkey_phys_table_len[port];
				     ++i) {
					ibdev->pkeys.virt2phys_pkey[slave][port - 1][i] =
					/* master has the identity virt2phys pkey mapping */
						(slave == mlx4_master_func_num(ibdev->dev) || !i) ? i :
							ibdev->dev->phys_caps.pkey_phys_table_len[port] - 1;
					mlx4_sync_pkey_table(ibdev->dev, slave, port, i,
							     ibdev->pkeys.virt2phys_pkey[slave][port - 1][i]);
				}
			}
		}
		/* initialize pkey cache */
		for (port = 1; port <= ibdev->dev->caps.num_ports; ++port) {
			for (i = 0;
			     i < ibdev->dev->phys_caps.pkey_phys_table_len[port];
			     ++i)
				ibdev->pkeys.phys_pkey_cache[port-1][i] =
					(i) ? 0 : 0xFFFF;
		}
	}
}

static void mlx4_ib_alloc_eqs(struct mlx4_dev *dev, struct mlx4_ib_dev *ibdev)
{
	char name[80];
	int eq_per_port = 0;
	int added_eqs = 0;
	int total_eqs = 0;
	int i, j, eq;

	/* Legacy mode or comp_pool is not large enough */
	if (dev->caps.comp_pool == 0 ||
	    dev->caps.num_ports > dev->caps.comp_pool)
		return;

	eq_per_port = rounddown_pow_of_two(dev->caps.comp_pool/
					dev->caps.num_ports);

	/* Init eq table */
	added_eqs = 0;
	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_IB)
		added_eqs += eq_per_port;

	total_eqs = dev->caps.num_comp_vectors + added_eqs;

	ibdev->eq_table = kzalloc(total_eqs * sizeof(int), GFP_KERNEL);
	if (!ibdev->eq_table)
		return;

	ibdev->eq_added = added_eqs;

	eq = 0;
	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_IB) {
		for (j = 0; j < eq_per_port; j++) {
			snprintf(name, sizeof(name), "mlx4-ib-%d-%d@%s",
				 i, j, dev->pdev->bus->name);
			/* Set IRQ for specific name (per ring) */
			if (mlx4_assign_eq(dev, name, NULL,
					   &ibdev->eq_table[eq])) {
				/* Use legacy (same as mlx4_en driver) */
				pr_warn("Can't allocate EQ %d; reverting to legacy\n", eq);
				ibdev->eq_table[eq] =
					(eq % dev->caps.num_comp_vectors);
			}
			eq++;
		}
	}

	/* Fill the reset of the vector with legacy EQ */
	for (i = 0, eq = added_eqs; i < dev->caps.num_comp_vectors; i++)
		ibdev->eq_table[eq++] = i;

	/* Advertise the new number of EQs to clients */
	ibdev->ib_dev.num_comp_vectors = total_eqs;
}

static void mlx4_ib_free_eqs(struct mlx4_dev *dev, struct mlx4_ib_dev *ibdev)
{
	int i;

	/* no additional eqs were added */
	if (!ibdev->eq_table)
		return;

	/* Reset the advertised EQ number */
	ibdev->ib_dev.num_comp_vectors = dev->caps.num_comp_vectors;

	/* Free only the added eqs */
	for (i = 0; i < ibdev->eq_added; i++) {
		/* Don't free legacy eqs if used */
		if (ibdev->eq_table[i] <= dev->caps.num_comp_vectors)
			continue;
		mlx4_release_eq(dev, ibdev->eq_table[i]);
	}

	kfree(ibdev->eq_table);
}

static void *mlx4_ib_add(struct mlx4_dev *dev)
{
	struct mlx4_ib_dev *ibdev;
	int num_ports = 0;
	int i, j;
	int err;
	struct mlx4_ib_iboe *iboe;
	int ib_num_ports = 0;

	pr_info_once("%s", mlx4_ib_version);

	num_ports = 0;
	mlx4_foreach_ib_transport_port(i, dev)
		num_ports++;

	/* No point in registering a device with no ports... */
	if (num_ports == 0)
		return NULL;

	ibdev = (struct mlx4_ib_dev *) ib_alloc_device(sizeof *ibdev);
	if (!ibdev) {
		dev_err(&dev->pdev->dev, "Device struct alloc failed\n");
		return NULL;
	}

	iboe = &ibdev->iboe;

	if (mlx4_pd_alloc(dev, &ibdev->priv_pdn))
		goto err_dealloc;

	if (mlx4_uar_alloc(dev, &ibdev->priv_uar))
		goto err_pd;

	ibdev->uar_map = ioremap((phys_addr_t) ibdev->priv_uar.pfn << PAGE_SHIFT,
				 PAGE_SIZE);
	if (!ibdev->uar_map)
		goto err_uar;
	MLX4_INIT_DOORBELL_LOCK(&ibdev->uar_lock);

	ibdev->dev = dev;

	strlcpy(ibdev->ib_dev.name, "mlx4_%d", IB_DEVICE_NAME_MAX);
	ibdev->ib_dev.owner		= THIS_MODULE;
	ibdev->ib_dev.node_type		= RDMA_NODE_IB_CA;
	ibdev->ib_dev.local_dma_lkey	= dev->caps.reserved_lkey;
	ibdev->num_ports		= num_ports;
	ibdev->ib_dev.phys_port_cnt     = ibdev->num_ports;
	ibdev->ib_dev.num_comp_vectors	= dev->caps.num_comp_vectors;
	ibdev->ib_dev.dma_device	= &dev->pdev->dev;

	if (dev->caps.userspace_caps)
		ibdev->ib_dev.uverbs_abi_ver = MLX4_IB_UVERBS_ABI_VERSION;
	else
		ibdev->ib_dev.uverbs_abi_ver = MLX4_IB_UVERBS_NO_DEV_CAPS_ABI_VERSION;

	ibdev->ib_dev.uverbs_cmd_mask	=
		(1ull << IB_USER_VERBS_CMD_GET_CONTEXT)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_DEVICE)	|
		(1ull << IB_USER_VERBS_CMD_QUERY_PORT)		|
		(1ull << IB_USER_VERBS_CMD_ALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_DEALLOC_PD)		|
		(1ull << IB_USER_VERBS_CMD_REG_MR)		|
		(1ull << IB_USER_VERBS_CMD_REREG_MR)		|
		(1ull << IB_USER_VERBS_CMD_DEREG_MR)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_COMP_CHANNEL)	|
		(1ull << IB_USER_VERBS_CMD_CREATE_CQ)		|
		(1ull << IB_USER_VERBS_CMD_RESIZE_CQ)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_CQ)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_QP)		|
		(1ull << IB_USER_VERBS_CMD_MODIFY_QP)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_QP)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_QP)		|
		(1ull << IB_USER_VERBS_CMD_ATTACH_MCAST)	|
		(1ull << IB_USER_VERBS_CMD_DETACH_MCAST)	|
		(1ull << IB_USER_VERBS_CMD_CREATE_SRQ)		|
		(1ull << IB_USER_VERBS_CMD_MODIFY_SRQ)		|
		(1ull << IB_USER_VERBS_CMD_QUERY_SRQ)		|
		(1ull << IB_USER_VERBS_CMD_DESTROY_SRQ)		|
		(1ull << IB_USER_VERBS_CMD_CREATE_XSRQ)		|
		(1ull << IB_USER_VERBS_CMD_OPEN_QP);

	ibdev->ib_dev.query_device	= mlx4_ib_query_device;
	ibdev->ib_dev.query_port	= mlx4_ib_query_port;
	ibdev->ib_dev.get_link_layer	= mlx4_ib_port_link_layer;
	ibdev->ib_dev.query_gid		= mlx4_ib_query_gid;
	ibdev->ib_dev.query_pkey	= mlx4_ib_query_pkey;
	ibdev->ib_dev.modify_device	= mlx4_ib_modify_device;
	ibdev->ib_dev.modify_port	= mlx4_ib_modify_port;
	ibdev->ib_dev.alloc_ucontext	= mlx4_ib_alloc_ucontext;
	ibdev->ib_dev.dealloc_ucontext	= mlx4_ib_dealloc_ucontext;
	ibdev->ib_dev.mmap		= mlx4_ib_mmap;
	ibdev->ib_dev.alloc_pd		= mlx4_ib_alloc_pd;
	ibdev->ib_dev.dealloc_pd	= mlx4_ib_dealloc_pd;
	ibdev->ib_dev.create_ah		= mlx4_ib_create_ah;
	ibdev->ib_dev.query_ah		= mlx4_ib_query_ah;
	ibdev->ib_dev.destroy_ah	= mlx4_ib_destroy_ah;
	ibdev->ib_dev.create_srq	= mlx4_ib_create_srq;
	ibdev->ib_dev.modify_srq	= mlx4_ib_modify_srq;
	ibdev->ib_dev.query_srq		= mlx4_ib_query_srq;
	ibdev->ib_dev.destroy_srq	= mlx4_ib_destroy_srq;
	ibdev->ib_dev.post_srq_recv	= mlx4_ib_post_srq_recv;
	ibdev->ib_dev.create_qp		= mlx4_ib_create_qp;
	ibdev->ib_dev.modify_qp		= mlx4_ib_modify_qp;
	ibdev->ib_dev.query_qp		= mlx4_ib_query_qp;
	ibdev->ib_dev.destroy_qp	= mlx4_ib_destroy_qp;
	ibdev->ib_dev.post_send		= mlx4_ib_post_send;
	ibdev->ib_dev.post_recv		= mlx4_ib_post_recv;
	ibdev->ib_dev.create_cq		= mlx4_ib_create_cq;
	ibdev->ib_dev.modify_cq		= mlx4_ib_modify_cq;
	ibdev->ib_dev.resize_cq		= mlx4_ib_resize_cq;
	ibdev->ib_dev.destroy_cq	= mlx4_ib_destroy_cq;
	ibdev->ib_dev.poll_cq		= mlx4_ib_poll_cq;
	ibdev->ib_dev.req_notify_cq	= mlx4_ib_arm_cq;
	ibdev->ib_dev.get_dma_mr	= mlx4_ib_get_dma_mr;
	ibdev->ib_dev.reg_user_mr	= mlx4_ib_reg_user_mr;
	ibdev->ib_dev.rereg_user_mr	= mlx4_ib_rereg_user_mr;
	ibdev->ib_dev.dereg_mr		= mlx4_ib_dereg_mr;
	ibdev->ib_dev.alloc_fast_reg_mr = mlx4_ib_alloc_fast_reg_mr;
	ibdev->ib_dev.alloc_fast_reg_page_list = mlx4_ib_alloc_fast_reg_page_list;
	ibdev->ib_dev.free_fast_reg_page_list  = mlx4_ib_free_fast_reg_page_list;
	ibdev->ib_dev.attach_mcast	= mlx4_ib_mcg_attach;
	ibdev->ib_dev.detach_mcast	= mlx4_ib_mcg_detach;
	ibdev->ib_dev.process_mad	= mlx4_ib_process_mad;

	if (!mlx4_is_slave(ibdev->dev)) {
		ibdev->ib_dev.alloc_fmr		= mlx4_ib_fmr_alloc;
		ibdev->ib_dev.map_phys_fmr	= mlx4_ib_map_phys_fmr;
		ibdev->ib_dev.unmap_fmr		= mlx4_ib_unmap_fmr;
		ibdev->ib_dev.dealloc_fmr	= mlx4_ib_fmr_dealloc;
	}

	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_MEM_WINDOW ||
	    dev->caps.bmme_flags & MLX4_BMME_FLAG_TYPE_2_WIN) {
		ibdev->ib_dev.alloc_mw = mlx4_ib_alloc_mw;
		ibdev->ib_dev.bind_mw = mlx4_ib_bind_mw;
		ibdev->ib_dev.dealloc_mw = mlx4_ib_dealloc_mw;

		ibdev->ib_dev.uverbs_cmd_mask |=
			(1ull << IB_USER_VERBS_CMD_ALLOC_MW) |
			(1ull << IB_USER_VERBS_CMD_DEALLOC_MW);
	}

	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC) {
		ibdev->ib_dev.alloc_xrcd = mlx4_ib_alloc_xrcd;
		ibdev->ib_dev.dealloc_xrcd = mlx4_ib_dealloc_xrcd;
		ibdev->ib_dev.uverbs_cmd_mask |=
			(1ull << IB_USER_VERBS_CMD_OPEN_XRCD) |
			(1ull << IB_USER_VERBS_CMD_CLOSE_XRCD);
	}

	if (check_flow_steering_support(dev)) {
		ibdev->steering_support = MLX4_STEERING_MODE_DEVICE_MANAGED;
		ibdev->ib_dev.create_flow	= mlx4_ib_create_flow;
		ibdev->ib_dev.destroy_flow	= mlx4_ib_destroy_flow;

		ibdev->ib_dev.uverbs_ex_cmd_mask	|=
			(1ull << IB_USER_VERBS_EX_CMD_CREATE_FLOW) |
			(1ull << IB_USER_VERBS_EX_CMD_DESTROY_FLOW);
	}

	mlx4_ib_alloc_eqs(dev, ibdev);

	spin_lock_init(&iboe->lock);

	if (init_node_data(ibdev))
		goto err_map;

	for (i = 0; i < ibdev->num_ports; ++i) {
		mutex_init(&ibdev->qp1_proxy_lock[i]);
		if (mlx4_ib_port_link_layer(&ibdev->ib_dev, i + 1) ==
						IB_LINK_LAYER_ETHERNET) {
			err = mlx4_counter_alloc(ibdev->dev, &ibdev->counters[i]);
			if (err)
				ibdev->counters[i] = -1;
		} else {
			ibdev->counters[i] = -1;
		}
	}

	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_IB)
		ib_num_ports++;

	spin_lock_init(&ibdev->sm_lock);
	mutex_init(&ibdev->cap_mask_mutex);

	if (ibdev->steering_support == MLX4_STEERING_MODE_DEVICE_MANAGED &&
	    ib_num_ports) {
		ibdev->steer_qpn_count = MLX4_IB_UC_MAX_NUM_QPS;
		err = mlx4_qp_reserve_range(dev, ibdev->steer_qpn_count,
					    MLX4_IB_UC_STEER_QPN_ALIGN,
					    &ibdev->steer_qpn_base);
		if (err)
			goto err_counter;

		ibdev->ib_uc_qpns_bitmap =
			kmalloc(BITS_TO_LONGS(ibdev->steer_qpn_count) *
				sizeof(long),
				GFP_KERNEL);
		if (!ibdev->ib_uc_qpns_bitmap) {
			dev_err(&dev->pdev->dev, "bit map alloc failed\n");
			goto err_steer_qp_release;
		}

		bitmap_zero(ibdev->ib_uc_qpns_bitmap, ibdev->steer_qpn_count);

		err = mlx4_FLOW_STEERING_IB_UC_QP_RANGE(
				dev, ibdev->steer_qpn_base,
				ibdev->steer_qpn_base +
				ibdev->steer_qpn_count - 1);
		if (err)
			goto err_steer_free_bitmap;
	}

	for (j = 1; j <= ibdev->dev->caps.num_ports; j++)
		atomic64_set(&iboe->mac[j - 1], ibdev->dev->caps.def_mac[j]);

	if (ib_register_device(&ibdev->ib_dev, NULL))
		goto err_steer_free_bitmap;

	if (mlx4_ib_mad_init(ibdev))
		goto err_reg;

	if (mlx4_ib_init_sriov(ibdev))
		goto err_mad;

	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_IBOE) {
		if (!iboe->nb.notifier_call) {
			iboe->nb.notifier_call = mlx4_ib_netdev_event;
			err = register_netdevice_notifier(&iboe->nb);
			if (err) {
				iboe->nb.notifier_call = NULL;
				goto err_notif;
			}
		}
		if (!iboe->nb_inet.notifier_call) {
			iboe->nb_inet.notifier_call = mlx4_ib_inet_event;
			err = register_inetaddr_notifier(&iboe->nb_inet);
			if (err) {
				iboe->nb_inet.notifier_call = NULL;
				goto err_notif;
			}
		}
#if IS_ENABLED(CONFIG_IPV6)
		if (!iboe->nb_inet6.notifier_call) {
			iboe->nb_inet6.notifier_call = mlx4_ib_inet6_event;
			err = register_inet6addr_notifier(&iboe->nb_inet6);
			if (err) {
				iboe->nb_inet6.notifier_call = NULL;
				goto err_notif;
			}
		}
#endif
		if (mlx4_ib_init_gid_table(ibdev))
			goto err_notif;
	}

	for (j = 0; j < ARRAY_SIZE(mlx4_class_attributes); ++j) {
		if (device_create_file(&ibdev->ib_dev.dev,
				       mlx4_class_attributes[j]))
			goto err_notif;
	}

	ibdev->ib_active = true;

	if (mlx4_is_mfunc(ibdev->dev))
		init_pkeys(ibdev);

	/* create paravirt contexts for any VFs which are active */
	if (mlx4_is_master(ibdev->dev)) {
		for (j = 0; j < MLX4_MFUNC_MAX; j++) {
			if (j == mlx4_master_func_num(ibdev->dev))
				continue;
			if (mlx4_is_slave_active(ibdev->dev, j))
				do_slave_init(ibdev, j, 1);
		}
	}
	return ibdev;

err_notif:
	if (ibdev->iboe.nb.notifier_call) {
		if (unregister_netdevice_notifier(&ibdev->iboe.nb))
			pr_warn("failure unregistering notifier\n");
		ibdev->iboe.nb.notifier_call = NULL;
	}
	if (ibdev->iboe.nb_inet.notifier_call) {
		if (unregister_inetaddr_notifier(&ibdev->iboe.nb_inet))
			pr_warn("failure unregistering notifier\n");
		ibdev->iboe.nb_inet.notifier_call = NULL;
	}
#if IS_ENABLED(CONFIG_IPV6)
	if (ibdev->iboe.nb_inet6.notifier_call) {
		if (unregister_inet6addr_notifier(&ibdev->iboe.nb_inet6))
			pr_warn("failure unregistering notifier\n");
		ibdev->iboe.nb_inet6.notifier_call = NULL;
	}
#endif
	flush_workqueue(wq);

	mlx4_ib_close_sriov(ibdev);

err_mad:
	mlx4_ib_mad_cleanup(ibdev);

err_reg:
	ib_unregister_device(&ibdev->ib_dev);

err_steer_free_bitmap:
	kfree(ibdev->ib_uc_qpns_bitmap);

err_steer_qp_release:
	if (ibdev->steering_support == MLX4_STEERING_MODE_DEVICE_MANAGED)
		mlx4_qp_release_range(dev, ibdev->steer_qpn_base,
				      ibdev->steer_qpn_count);
err_counter:
	for (; i; --i)
		if (ibdev->counters[i - 1] != -1)
			mlx4_counter_free(ibdev->dev, ibdev->counters[i - 1]);

err_map:
	iounmap(ibdev->uar_map);

err_uar:
	mlx4_uar_free(dev, &ibdev->priv_uar);

err_pd:
	mlx4_pd_free(dev, ibdev->priv_pdn);

err_dealloc:
	ib_dealloc_device(&ibdev->ib_dev);

	return NULL;
}

int mlx4_ib_steer_qp_alloc(struct mlx4_ib_dev *dev, int count, int *qpn)
{
	int offset;

	WARN_ON(!dev->ib_uc_qpns_bitmap);

	offset = bitmap_find_free_region(dev->ib_uc_qpns_bitmap,
					 dev->steer_qpn_count,
					 get_count_order(count));
	if (offset < 0)
		return offset;

	*qpn = dev->steer_qpn_base + offset;
	return 0;
}

void mlx4_ib_steer_qp_free(struct mlx4_ib_dev *dev, u32 qpn, int count)
{
	if (!qpn ||
	    dev->steering_support != MLX4_STEERING_MODE_DEVICE_MANAGED)
		return;

	BUG_ON(qpn < dev->steer_qpn_base);

	bitmap_release_region(dev->ib_uc_qpns_bitmap,
			      qpn - dev->steer_qpn_base,
			      get_count_order(count));
}

int mlx4_ib_steer_qp_reg(struct mlx4_ib_dev *mdev, struct mlx4_ib_qp *mqp,
			 int is_attach)
{
	int err;
	size_t flow_size;
	struct ib_flow_attr *flow = NULL;
	struct ib_flow_spec_ib *ib_spec;

	if (is_attach) {
		flow_size = sizeof(struct ib_flow_attr) +
			    sizeof(struct ib_flow_spec_ib);
		flow = kzalloc(flow_size, GFP_KERNEL);
		if (!flow)
			return -ENOMEM;
		flow->port = mqp->port;
		flow->num_of_specs = 1;
		flow->size = flow_size;
		ib_spec = (struct ib_flow_spec_ib *)(flow + 1);
		ib_spec->type = IB_FLOW_SPEC_IB;
		ib_spec->size = sizeof(struct ib_flow_spec_ib);
		/* Add an empty rule for IB L2 */
		memset(&ib_spec->mask, 0, sizeof(ib_spec->mask));

		err = __mlx4_ib_create_flow(&mqp->ibqp, flow,
					    IB_FLOW_DOMAIN_NIC,
					    MLX4_FS_REGULAR,
					    &mqp->reg_id);
	} else {
		err = __mlx4_ib_destroy_flow(mdev->dev, mqp->reg_id);
	}
	kfree(flow);
	return err;
}

static void mlx4_ib_remove(struct mlx4_dev *dev, void *ibdev_ptr)
{
	struct mlx4_ib_dev *ibdev = ibdev_ptr;
	int p;

	ibdev->ib_active = false;
	flush_workqueue(wq);

	mlx4_ib_close_sriov(ibdev);
	mlx4_ib_mad_cleanup(ibdev);
	ib_unregister_device(&ibdev->ib_dev);
	if (ibdev->iboe.nb.notifier_call) {
		if (unregister_netdevice_notifier(&ibdev->iboe.nb))
			pr_warn("failure unregistering notifier\n");
		ibdev->iboe.nb.notifier_call = NULL;
	}

	if (ibdev->steering_support == MLX4_STEERING_MODE_DEVICE_MANAGED) {
		mlx4_qp_release_range(dev, ibdev->steer_qpn_base,
				      ibdev->steer_qpn_count);
		kfree(ibdev->ib_uc_qpns_bitmap);
	}

	if (ibdev->iboe.nb_inet.notifier_call) {
		if (unregister_inetaddr_notifier(&ibdev->iboe.nb_inet))
			pr_warn("failure unregistering notifier\n");
		ibdev->iboe.nb_inet.notifier_call = NULL;
	}
#if IS_ENABLED(CONFIG_IPV6)
	if (ibdev->iboe.nb_inet6.notifier_call) {
		if (unregister_inet6addr_notifier(&ibdev->iboe.nb_inet6))
			pr_warn("failure unregistering notifier\n");
		ibdev->iboe.nb_inet6.notifier_call = NULL;
	}
#endif

	iounmap(ibdev->uar_map);
	for (p = 0; p < ibdev->num_ports; ++p)
		if (ibdev->counters[p] != -1)
			mlx4_counter_free(ibdev->dev, ibdev->counters[p]);
	mlx4_foreach_port(p, dev, MLX4_PORT_TYPE_IB)
		mlx4_CLOSE_PORT(dev, p);

	mlx4_ib_free_eqs(dev, ibdev);

	mlx4_uar_free(dev, &ibdev->priv_uar);
	mlx4_pd_free(dev, ibdev->priv_pdn);
	ib_dealloc_device(&ibdev->ib_dev);
}

static void do_slave_init(struct mlx4_ib_dev *ibdev, int slave, int do_init)
{
	struct mlx4_ib_demux_work **dm = NULL;
	struct mlx4_dev *dev = ibdev->dev;
	int i;
	unsigned long flags;
	struct mlx4_active_ports actv_ports;
	unsigned int ports;
	unsigned int first_port;

	if (!mlx4_is_master(dev))
		return;

	actv_ports = mlx4_get_active_ports(dev, slave);
	ports = bitmap_weight(actv_ports.ports, dev->caps.num_ports);
	first_port = find_first_bit(actv_ports.ports, dev->caps.num_ports);

	dm = kcalloc(ports, sizeof(*dm), GFP_ATOMIC);
	if (!dm) {
		pr_err("failed to allocate memory for tunneling qp update\n");
		goto out;
	}

	for (i = 0; i < ports; i++) {
		dm[i] = kmalloc(sizeof (struct mlx4_ib_demux_work), GFP_ATOMIC);
		if (!dm[i]) {
			pr_err("failed to allocate memory for tunneling qp update work struct\n");
			for (i = 0; i < dev->caps.num_ports; i++) {
				if (dm[i])
					kfree(dm[i]);
			}
			goto out;
		}
	}
	/* initialize or tear down tunnel QPs for the slave */
	for (i = 0; i < ports; i++) {
		INIT_WORK(&dm[i]->work, mlx4_ib_tunnels_update_work);
		dm[i]->port = first_port + i + 1;
		dm[i]->slave = slave;
		dm[i]->do_init = do_init;
		dm[i]->dev = ibdev;
		spin_lock_irqsave(&ibdev->sriov.going_down_lock, flags);
		if (!ibdev->sriov.is_going_down)
			queue_work(ibdev->sriov.demux[i].ud_wq, &dm[i]->work);
		spin_unlock_irqrestore(&ibdev->sriov.going_down_lock, flags);
	}
out:
	kfree(dm);
	return;
}

static void mlx4_ib_event(struct mlx4_dev *dev, void *ibdev_ptr,
			  enum mlx4_dev_event event, unsigned long param)
{
	struct ib_event ibev;
	struct mlx4_ib_dev *ibdev = to_mdev((struct ib_device *) ibdev_ptr);
	struct mlx4_eqe *eqe = NULL;
	struct ib_event_work *ew;
	int p = 0;

	if (event == MLX4_DEV_EVENT_PORT_MGMT_CHANGE)
		eqe = (struct mlx4_eqe *)param;
	else
		p = (int) param;

	switch (event) {
	case MLX4_DEV_EVENT_PORT_UP:
		if (p > ibdev->num_ports)
			return;
		if (mlx4_is_master(dev) &&
		    rdma_port_get_link_layer(&ibdev->ib_dev, p) ==
			IB_LINK_LAYER_INFINIBAND) {
			mlx4_ib_invalidate_all_guid_record(ibdev, p);
		}
		ibev.event = IB_EVENT_PORT_ACTIVE;
		break;

	case MLX4_DEV_EVENT_PORT_DOWN:
		if (p > ibdev->num_ports)
			return;
		ibev.event = IB_EVENT_PORT_ERR;
		break;

	case MLX4_DEV_EVENT_CATASTROPHIC_ERROR:
		ibdev->ib_active = false;
		ibev.event = IB_EVENT_DEVICE_FATAL;
		break;

	case MLX4_DEV_EVENT_PORT_MGMT_CHANGE:
		ew = kmalloc(sizeof *ew, GFP_ATOMIC);
		if (!ew) {
			pr_err("failed to allocate memory for events work\n");
			break;
		}

		INIT_WORK(&ew->work, handle_port_mgmt_change_event);
		memcpy(&ew->ib_eqe, eqe, sizeof *eqe);
		ew->ib_dev = ibdev;
		/* need to queue only for port owner, which uses GEN_EQE */
		if (mlx4_is_master(dev))
			queue_work(wq, &ew->work);
		else
			handle_port_mgmt_change_event(&ew->work);
		return;

	case MLX4_DEV_EVENT_SLAVE_INIT:
		/* here, p is the slave id */
		do_slave_init(ibdev, p, 1);
		return;

	case MLX4_DEV_EVENT_SLAVE_SHUTDOWN:
		/* here, p is the slave id */
		do_slave_init(ibdev, p, 0);
		return;

	default:
		return;
	}

	ibev.device	      = ibdev_ptr;
	ibev.element.port_num = (u8) p;

	ib_dispatch_event(&ibev);
}

static struct mlx4_interface mlx4_ib_interface = {
	.add		= mlx4_ib_add,
	.remove		= mlx4_ib_remove,
	.event		= mlx4_ib_event,
	.protocol	= MLX4_PROT_IB_IPV6
};

static int __init mlx4_ib_init(void)
{
	int err;

	wq = create_singlethread_workqueue("mlx4_ib");
	if (!wq)
		return -ENOMEM;

	err = mlx4_ib_mcg_init();
	if (err)
		goto clean_wq;

	err = mlx4_register_interface(&mlx4_ib_interface);
	if (err)
		goto clean_mcg;

	return 0;

clean_mcg:
	mlx4_ib_mcg_destroy();

clean_wq:
	destroy_workqueue(wq);
	return err;
}

static void __exit mlx4_ib_cleanup(void)
{
	mlx4_unregister_interface(&mlx4_ib_interface);
	mlx4_ib_mcg_destroy();
	destroy_workqueue(wq);
}

module_init(mlx4_ib_init);
module_exit(mlx4_ib_cleanup);
