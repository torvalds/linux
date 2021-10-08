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
#include <linux/sched/mm.h>
#include <linux/sched/task.h>

#include <net/ipv6.h>
#include <net/addrconf.h>
#include <net/devlink.h>

#include <rdma/ib_smi.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>

#include <net/bonding.h>

#include <linux/mlx4/driver.h>
#include <linux/mlx4/cmd.h>
#include <linux/mlx4/qp.h>

#include "mlx4_ib.h"
#include <rdma/mlx4-abi.h>

#define DRV_NAME	MLX4_IB_DRV_NAME
#define DRV_VERSION	"4.0-0"

#define MLX4_IB_FLOW_MAX_PRIO 0xFFF
#define MLX4_IB_FLOW_QPN_MASK 0xFFFFFF
#define MLX4_IB_CARD_REV_A0   0xA0

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("Mellanox ConnectX HCA InfiniBand driver");
MODULE_LICENSE("Dual BSD/GPL");

int mlx4_ib_sm_guid_assign = 0;
module_param_named(sm_guid_assign, mlx4_ib_sm_guid_assign, int, 0444);
MODULE_PARM_DESC(sm_guid_assign, "Enable SM alias_GUID assignment if sm_guid_assign > 0 (Default: 0)");

static const char mlx4_ib_version[] =
	DRV_NAME ": Mellanox ConnectX InfiniBand driver v"
	DRV_VERSION "\n";

static void do_slave_init(struct mlx4_ib_dev *ibdev, int slave, int do_init);
static enum rdma_link_layer mlx4_ib_port_link_layer(struct ib_device *device,
						    u32 port_num);

static struct workqueue_struct *wq;

static void init_query_mad(struct ib_smp *mad)
{
	mad->base_version  = 1;
	mad->mgmt_class    = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	mad->class_version = 1;
	mad->method	   = IB_MGMT_METHOD_GET;
}

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

static struct net_device *mlx4_ib_get_netdev(struct ib_device *device,
					     u32 port_num)
{
	struct mlx4_ib_dev *ibdev = to_mdev(device);
	struct net_device *dev;

	rcu_read_lock();
	dev = mlx4_get_protocol_dev(ibdev->dev, MLX4_PROT_ETH, port_num);

	if (dev) {
		if (mlx4_is_bonded(ibdev->dev)) {
			struct net_device *upper = NULL;

			upper = netdev_master_upper_dev_get_rcu(dev);
			if (upper) {
				struct net_device *active;

				active = bond_option_active_slave_get_rcu(netdev_priv(upper));
				if (active)
					dev = active;
			}
		}
	}
	if (dev)
		dev_hold(dev);

	rcu_read_unlock();
	return dev;
}

static int mlx4_ib_update_gids_v1(struct gid_entry *gids,
				  struct mlx4_ib_dev *ibdev,
				  u32 port_num)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err;
	struct mlx4_dev *dev = ibdev->dev;
	int i;
	union ib_gid *gid_tbl;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return -ENOMEM;

	gid_tbl = mailbox->buf;

	for (i = 0; i < MLX4_MAX_PORT_GIDS; ++i)
		memcpy(&gid_tbl[i], &gids[i].gid, sizeof(union ib_gid));

	err = mlx4_cmd(dev, mailbox->dma,
		       MLX4_SET_PORT_GID_TABLE << 8 | port_num,
		       1, MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_WRAPPED);
	if (mlx4_is_bonded(dev))
		err += mlx4_cmd(dev, mailbox->dma,
				MLX4_SET_PORT_GID_TABLE << 8 | 2,
				1, MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
				MLX4_CMD_WRAPPED);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

static int mlx4_ib_update_gids_v1_v2(struct gid_entry *gids,
				     struct mlx4_ib_dev *ibdev,
				     u32 port_num)
{
	struct mlx4_cmd_mailbox *mailbox;
	int err;
	struct mlx4_dev *dev = ibdev->dev;
	int i;
	struct {
		union ib_gid	gid;
		__be32		rsrvd1[2];
		__be16		rsrvd2;
		u8		type;
		u8		version;
		__be32		rsrvd3;
	} *gid_tbl;

	mailbox = mlx4_alloc_cmd_mailbox(dev);
	if (IS_ERR(mailbox))
		return -ENOMEM;

	gid_tbl = mailbox->buf;
	for (i = 0; i < MLX4_MAX_PORT_GIDS; ++i) {
		memcpy(&gid_tbl[i].gid, &gids[i].gid, sizeof(union ib_gid));
		if (gids[i].gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP) {
			gid_tbl[i].version = 2;
			if (!ipv6_addr_v4mapped((struct in6_addr *)&gids[i].gid))
				gid_tbl[i].type = 1;
		}
	}

	err = mlx4_cmd(dev, mailbox->dma,
		       MLX4_SET_PORT_ROCE_ADDR << 8 | port_num,
		       1, MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_WRAPPED);
	if (mlx4_is_bonded(dev))
		err += mlx4_cmd(dev, mailbox->dma,
				MLX4_SET_PORT_ROCE_ADDR << 8 | 2,
				1, MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
				MLX4_CMD_WRAPPED);

	mlx4_free_cmd_mailbox(dev, mailbox);
	return err;
}

static int mlx4_ib_update_gids(struct gid_entry *gids,
			       struct mlx4_ib_dev *ibdev,
			       u32 port_num)
{
	if (ibdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ROCE_V1_V2)
		return mlx4_ib_update_gids_v1_v2(gids, ibdev, port_num);

	return mlx4_ib_update_gids_v1(gids, ibdev, port_num);
}

static void free_gid_entry(struct gid_entry *entry)
{
	memset(&entry->gid, 0, sizeof(entry->gid));
	kfree(entry->ctx);
	entry->ctx = NULL;
}

static int mlx4_ib_add_gid(const struct ib_gid_attr *attr, void **context)
{
	struct mlx4_ib_dev *ibdev = to_mdev(attr->device);
	struct mlx4_ib_iboe *iboe = &ibdev->iboe;
	struct mlx4_port_gid_table   *port_gid_table;
	int free = -1, found = -1;
	int ret = 0;
	int hw_update = 0;
	int i;
	struct gid_entry *gids = NULL;
	u16 vlan_id = 0xffff;
	u8 mac[ETH_ALEN];

	if (!rdma_cap_roce_gid_table(attr->device, attr->port_num))
		return -EINVAL;

	if (attr->port_num > MLX4_MAX_PORTS)
		return -EINVAL;

	if (!context)
		return -EINVAL;

	ret = rdma_read_gid_l2_fields(attr, &vlan_id, &mac[0]);
	if (ret)
		return ret;
	port_gid_table = &iboe->gids[attr->port_num - 1];
	spin_lock_bh(&iboe->lock);
	for (i = 0; i < MLX4_MAX_PORT_GIDS; ++i) {
		if (!memcmp(&port_gid_table->gids[i].gid,
			    &attr->gid, sizeof(attr->gid)) &&
		    port_gid_table->gids[i].gid_type == attr->gid_type &&
		    port_gid_table->gids[i].vlan_id == vlan_id)  {
			found = i;
			break;
		}
		if (free < 0 && rdma_is_zero_gid(&port_gid_table->gids[i].gid))
			free = i; /* HW has space */
	}

	if (found < 0) {
		if (free < 0) {
			ret = -ENOSPC;
		} else {
			port_gid_table->gids[free].ctx = kmalloc(sizeof(*port_gid_table->gids[free].ctx), GFP_ATOMIC);
			if (!port_gid_table->gids[free].ctx) {
				ret = -ENOMEM;
			} else {
				*context = port_gid_table->gids[free].ctx;
				memcpy(&port_gid_table->gids[free].gid,
				       &attr->gid, sizeof(attr->gid));
				port_gid_table->gids[free].gid_type = attr->gid_type;
				port_gid_table->gids[free].vlan_id = vlan_id;
				port_gid_table->gids[free].ctx->real_index = free;
				port_gid_table->gids[free].ctx->refcount = 1;
				hw_update = 1;
			}
		}
	} else {
		struct gid_cache_context *ctx = port_gid_table->gids[found].ctx;
		*context = ctx;
		ctx->refcount++;
	}
	if (!ret && hw_update) {
		gids = kmalloc_array(MLX4_MAX_PORT_GIDS, sizeof(*gids),
				     GFP_ATOMIC);
		if (!gids) {
			ret = -ENOMEM;
			*context = NULL;
			free_gid_entry(&port_gid_table->gids[free]);
		} else {
			for (i = 0; i < MLX4_MAX_PORT_GIDS; i++) {
				memcpy(&gids[i].gid, &port_gid_table->gids[i].gid, sizeof(union ib_gid));
				gids[i].gid_type = port_gid_table->gids[i].gid_type;
			}
		}
	}
	spin_unlock_bh(&iboe->lock);

	if (!ret && hw_update) {
		ret = mlx4_ib_update_gids(gids, ibdev, attr->port_num);
		if (ret) {
			spin_lock_bh(&iboe->lock);
			*context = NULL;
			free_gid_entry(&port_gid_table->gids[free]);
			spin_unlock_bh(&iboe->lock);
		}
		kfree(gids);
	}

	return ret;
}

static int mlx4_ib_del_gid(const struct ib_gid_attr *attr, void **context)
{
	struct gid_cache_context *ctx = *context;
	struct mlx4_ib_dev *ibdev = to_mdev(attr->device);
	struct mlx4_ib_iboe *iboe = &ibdev->iboe;
	struct mlx4_port_gid_table   *port_gid_table;
	int ret = 0;
	int hw_update = 0;
	struct gid_entry *gids = NULL;

	if (!rdma_cap_roce_gid_table(attr->device, attr->port_num))
		return -EINVAL;

	if (attr->port_num > MLX4_MAX_PORTS)
		return -EINVAL;

	port_gid_table = &iboe->gids[attr->port_num - 1];
	spin_lock_bh(&iboe->lock);
	if (ctx) {
		ctx->refcount--;
		if (!ctx->refcount) {
			unsigned int real_index = ctx->real_index;

			free_gid_entry(&port_gid_table->gids[real_index]);
			hw_update = 1;
		}
	}
	if (!ret && hw_update) {
		int i;

		gids = kmalloc_array(MLX4_MAX_PORT_GIDS, sizeof(*gids),
				     GFP_ATOMIC);
		if (!gids) {
			ret = -ENOMEM;
		} else {
			for (i = 0; i < MLX4_MAX_PORT_GIDS; i++) {
				memcpy(&gids[i].gid,
				       &port_gid_table->gids[i].gid,
				       sizeof(union ib_gid));
				gids[i].gid_type =
				    port_gid_table->gids[i].gid_type;
			}
		}
	}
	spin_unlock_bh(&iboe->lock);

	if (!ret && hw_update) {
		ret = mlx4_ib_update_gids(gids, ibdev, attr->port_num);
		kfree(gids);
	}
	return ret;
}

int mlx4_ib_gid_index_to_real_index(struct mlx4_ib_dev *ibdev,
				    const struct ib_gid_attr *attr)
{
	struct mlx4_ib_iboe *iboe = &ibdev->iboe;
	struct gid_cache_context *ctx = NULL;
	struct mlx4_port_gid_table   *port_gid_table;
	int real_index = -EINVAL;
	int i;
	unsigned long flags;
	u32 port_num = attr->port_num;

	if (port_num > MLX4_MAX_PORTS)
		return -EINVAL;

	if (mlx4_is_bonded(ibdev->dev))
		port_num = 1;

	if (!rdma_cap_roce_gid_table(&ibdev->ib_dev, port_num))
		return attr->index;

	spin_lock_irqsave(&iboe->lock, flags);
	port_gid_table = &iboe->gids[port_num - 1];

	for (i = 0; i < MLX4_MAX_PORT_GIDS; ++i)
		if (!memcmp(&port_gid_table->gids[i].gid,
			    &attr->gid, sizeof(attr->gid)) &&
		    attr->gid_type == port_gid_table->gids[i].gid_type) {
			ctx = port_gid_table->gids[i].ctx;
			break;
		}
	if (ctx)
		real_index = ctx->real_index;
	spin_unlock_irqrestore(&iboe->lock, flags);
	return real_index;
}

static int mlx4_ib_query_device(struct ib_device *ibdev,
				struct ib_device_attr *props,
				struct ib_udata *uhw)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int err;
	int have_ib_ports;
	struct mlx4_uverbs_ex_query_device cmd;
	struct mlx4_uverbs_ex_query_device_resp resp = {};
	struct mlx4_clock_params clock_params;

	if (uhw->inlen) {
		if (uhw->inlen < sizeof(cmd))
			return -EINVAL;

		err = ib_copy_from_udata(&cmd, uhw, sizeof(cmd));
		if (err)
			return err;

		if (cmd.comp_mask)
			return -EINVAL;

		if (cmd.reserved)
			return -EINVAL;
	}

	resp.response_length = offsetof(typeof(resp), response_length) +
		sizeof(resp.response_length);
	in_mad  = kzalloc(sizeof *in_mad, GFP_KERNEL);
	out_mad = kmalloc(sizeof *out_mad, GFP_KERNEL);
	err = -ENOMEM;
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
	}
	if (dev->steering_support == MLX4_STEERING_MODE_DEVICE_MANAGED)
		props->device_cap_flags |= IB_DEVICE_MANAGED_FLOW_STEERING;

	props->device_cap_flags |= IB_DEVICE_RAW_IP_CSUM;

	props->vendor_id	   = be32_to_cpup((__be32 *) (out_mad->data + 36)) &
		0xffffff;
	props->vendor_part_id	   = dev->dev->persist->pdev->device;
	props->hw_ver		   = be32_to_cpup((__be32 *) (out_mad->data + 32));
	memcpy(&props->sys_image_guid, out_mad->data +	4, 8);

	props->max_mr_size	   = ~0ull;
	props->page_size_cap	   = dev->dev->caps.page_size_cap;
	props->max_qp		   = dev->dev->quotas.qp;
	props->max_qp_wr	   = dev->dev->caps.max_wqes - MLX4_IB_SQ_MAX_SPARE;
	props->max_send_sge =
		min(dev->dev->caps.max_sq_sg, dev->dev->caps.max_rq_sg);
	props->max_recv_sge =
		min(dev->dev->caps.max_sq_sg, dev->dev->caps.max_rq_sg);
	props->max_sge_rd = MLX4_MAX_SGE_RD;
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
	props->hca_core_clock = dev->dev->caps.hca_core_clock * 1000UL;
	props->timestamp_mask = 0xFFFFFFFFFFFFULL;
	props->max_ah = INT_MAX;

	if (mlx4_ib_port_link_layer(ibdev, 1) == IB_LINK_LAYER_ETHERNET ||
	    mlx4_ib_port_link_layer(ibdev, 2) == IB_LINK_LAYER_ETHERNET) {
		if (dev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS) {
			props->rss_caps.max_rwq_indirection_tables =
				props->max_qp;
			props->rss_caps.max_rwq_indirection_table_size =
				dev->dev->caps.max_rss_tbl_sz;
			props->rss_caps.supported_qpts = 1 << IB_QPT_RAW_PACKET;
			props->max_wq_type_rq = props->max_qp;
		}

		if (dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_FCS_KEEP)
			props->raw_packet_caps |= IB_RAW_PACKET_CAP_SCATTER_FCS;
	}

	props->cq_caps.max_cq_moderation_count = MLX4_MAX_CQ_COUNT;
	props->cq_caps.max_cq_moderation_period = MLX4_MAX_CQ_PERIOD;

	if (uhw->outlen >= resp.response_length + sizeof(resp.hca_core_clock_offset)) {
		resp.response_length += sizeof(resp.hca_core_clock_offset);
		if (!mlx4_get_internal_clock_params(dev->dev, &clock_params)) {
			resp.comp_mask |= MLX4_IB_QUERY_DEV_RESP_MASK_CORE_CLOCK_OFFSET;
			resp.hca_core_clock_offset = clock_params.offset % PAGE_SIZE;
		}
	}

	if (uhw->outlen >= resp.response_length +
	    sizeof(resp.max_inl_recv_sz)) {
		resp.response_length += sizeof(resp.max_inl_recv_sz);
		resp.max_inl_recv_sz  = dev->dev->caps.max_rq_sg *
			sizeof(struct mlx4_wqe_data_seg);
	}

	if (offsetofend(typeof(resp), rss_caps) <= uhw->outlen) {
		if (props->rss_caps.supported_qpts) {
			resp.rss_caps.rx_hash_function =
				MLX4_IB_RX_HASH_FUNC_TOEPLITZ;

			resp.rss_caps.rx_hash_fields_mask =
				MLX4_IB_RX_HASH_SRC_IPV4 |
				MLX4_IB_RX_HASH_DST_IPV4 |
				MLX4_IB_RX_HASH_SRC_IPV6 |
				MLX4_IB_RX_HASH_DST_IPV6 |
				MLX4_IB_RX_HASH_SRC_PORT_TCP |
				MLX4_IB_RX_HASH_DST_PORT_TCP |
				MLX4_IB_RX_HASH_SRC_PORT_UDP |
				MLX4_IB_RX_HASH_DST_PORT_UDP;

			if (dev->dev->caps.tunnel_offload_mode ==
			    MLX4_TUNNEL_OFFLOAD_MODE_VXLAN)
				resp.rss_caps.rx_hash_fields_mask |=
					MLX4_IB_RX_HASH_INNER;
		}
		resp.response_length = offsetof(typeof(resp), rss_caps) +
				       sizeof(resp.rss_caps);
	}

	if (offsetofend(typeof(resp), tso_caps) <= uhw->outlen) {
		if (dev->dev->caps.max_gso_sz &&
		    ((mlx4_ib_port_link_layer(ibdev, 1) ==
		    IB_LINK_LAYER_ETHERNET) ||
		    (mlx4_ib_port_link_layer(ibdev, 2) ==
		    IB_LINK_LAYER_ETHERNET))) {
			resp.tso_caps.max_tso = dev->dev->caps.max_gso_sz;
			resp.tso_caps.supported_qpts |=
				1 << IB_QPT_RAW_PACKET;
		}
		resp.response_length = offsetof(typeof(resp), tso_caps) +
				       sizeof(resp.tso_caps);
	}

	if (uhw->outlen) {
		err = ib_copy_to_udata(uhw, &resp, resp.response_length);
		if (err)
			goto out;
	}
out:
	kfree(in_mad);
	kfree(out_mad);

	return err;
}

static enum rdma_link_layer
mlx4_ib_port_link_layer(struct ib_device *device, u32 port_num)
{
	struct mlx4_dev *dev = to_mdev(device)->dev;

	return dev->caps.port_mask[port_num] == MLX4_PORT_TYPE_IB ?
		IB_LINK_LAYER_INFINIBAND : IB_LINK_LAYER_ETHERNET;
}

static int ib_link_query_port(struct ib_device *ibdev, u32 port,
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
	return state == IB_PORT_ACTIVE ?
		IB_PORT_PHYS_STATE_LINK_UP : IB_PORT_PHYS_STATE_DISABLED;
}

static int eth_link_query_port(struct ib_device *ibdev, u32 port,
			       struct ib_port_attr *props)
{

	struct mlx4_ib_dev *mdev = to_mdev(ibdev);
	struct mlx4_ib_iboe *iboe = &mdev->iboe;
	struct net_device *ndev;
	enum ib_mtu tmp;
	struct mlx4_cmd_mailbox *mailbox;
	int err = 0;
	int is_bonded = mlx4_is_bonded(mdev->dev);

	mailbox = mlx4_alloc_cmd_mailbox(mdev->dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	err = mlx4_cmd_box(mdev->dev, 0, mailbox->dma, port, 0,
			   MLX4_CMD_QUERY_PORT, MLX4_CMD_TIME_CLASS_B,
			   MLX4_CMD_WRAPPED);
	if (err)
		goto out;

	props->active_width	=  (((u8 *)mailbox->buf)[5] == 0x40) ||
				   (((u8 *)mailbox->buf)[5] == 0x20 /*56Gb*/) ?
					   IB_WIDTH_4X : IB_WIDTH_1X;
	props->active_speed	=  (((u8 *)mailbox->buf)[5] == 0x20 /*56Gb*/) ?
					   IB_SPEED_FDR : IB_SPEED_QDR;
	props->port_cap_flags	= IB_PORT_CM_SUP;
	props->ip_gids = true;
	props->gid_tbl_len	= mdev->dev->caps.gid_table_len[port];
	props->max_msg_sz	= mdev->dev->caps.max_msg_sz;
	if (mdev->dev->caps.pkey_table_len[port])
		props->pkey_tbl_len = 1;
	props->max_mtu		= IB_MTU_4096;
	props->max_vl_num	= 2;
	props->state		= IB_PORT_DOWN;
	props->phys_state	= state_to_phys_state(props->state);
	props->active_mtu	= IB_MTU_256;
	spin_lock_bh(&iboe->lock);
	ndev = iboe->netdevs[port - 1];
	if (ndev && is_bonded) {
		rcu_read_lock(); /* required to get upper dev */
		ndev = netdev_master_upper_dev_get_rcu(ndev);
		rcu_read_unlock();
	}
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

int __mlx4_ib_query_port(struct ib_device *ibdev, u32 port,
			 struct ib_port_attr *props, int netw_view)
{
	int err;

	/* props being zeroed by the caller, avoid zeroing it here */

	err = mlx4_ib_port_link_layer(ibdev, port) == IB_LINK_LAYER_INFINIBAND ?
		ib_link_query_port(ibdev, port, props, netw_view) :
				eth_link_query_port(ibdev, port, props);

	return err;
}

static int mlx4_ib_query_port(struct ib_device *ibdev, u32 port,
			      struct ib_port_attr *props)
{
	/* returns host view */
	return __mlx4_ib_query_port(ibdev, port, props, 0);
}

int __mlx4_ib_query_gid(struct ib_device *ibdev, u32 port, int index,
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

static int mlx4_ib_query_gid(struct ib_device *ibdev, u32 port, int index,
			     union ib_gid *gid)
{
	if (rdma_protocol_ib(ibdev, port))
		return __mlx4_ib_query_gid(ibdev, port, index, gid, 0);
	return 0;
}

static int mlx4_ib_query_sl2vl(struct ib_device *ibdev, u32 port,
			       u64 *sl2vl_tbl)
{
	union sl2vl_tbl_to_u64 sl2vl64;
	struct ib_smp *in_mad  = NULL;
	struct ib_smp *out_mad = NULL;
	int mad_ifc_flags = MLX4_MAD_IFC_IGNORE_KEYS;
	int err = -ENOMEM;
	int jj;

	if (mlx4_is_slave(to_mdev(ibdev)->dev)) {
		*sl2vl_tbl = 0;
		return 0;
	}

	in_mad  = kzalloc(sizeof(*in_mad), GFP_KERNEL);
	out_mad = kmalloc(sizeof(*out_mad), GFP_KERNEL);
	if (!in_mad || !out_mad)
		goto out;

	init_query_mad(in_mad);
	in_mad->attr_id  = IB_SMP_ATTR_SL_TO_VL_TABLE;
	in_mad->attr_mod = 0;

	if (mlx4_is_mfunc(to_mdev(ibdev)->dev))
		mad_ifc_flags |= MLX4_MAD_IFC_NET_VIEW;

	err = mlx4_MAD_IFC(to_mdev(ibdev), mad_ifc_flags, port, NULL, NULL,
			   in_mad, out_mad);
	if (err)
		goto out;

	for (jj = 0; jj < 8; jj++)
		sl2vl64.sl8[jj] = ((struct ib_smp *)out_mad)->data[jj];
	*sl2vl_tbl = sl2vl64.sl64;

out:
	kfree(in_mad);
	kfree(out_mad);
	return err;
}

static void mlx4_init_sl2vl_tbl(struct mlx4_ib_dev *mdev)
{
	u64 sl2vl;
	int i;
	int err;

	for (i = 1; i <= mdev->dev->caps.num_ports; i++) {
		if (mdev->dev->caps.port_type[i] == MLX4_PORT_TYPE_ETH)
			continue;
		err = mlx4_ib_query_sl2vl(&mdev->ib_dev, i, &sl2vl);
		if (err) {
			pr_err("Unable to get default sl to vl mapping for port %d.  Using all zeroes (%d)\n",
			       i, err);
			sl2vl = 0;
		}
		atomic64_set(&mdev->sl2vl[i - 1], sl2vl);
	}
}

int __mlx4_ib_query_pkey(struct ib_device *ibdev, u32 port, u16 index,
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

static int mlx4_ib_query_pkey(struct ib_device *ibdev, u32 port, u16 index,
			      u16 *pkey)
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
	memcpy(ibdev->node_desc, props->node_desc, IB_DEVICE_NODE_DESC_MAX);
	spin_unlock_irqrestore(&to_mdev(ibdev)->sm_lock, flags);

	/*
	 * If possible, pass node desc to FW, so it can generate
	 * a 144 trap.  If cmd fails, just ignore.
	 */
	mailbox = mlx4_alloc_cmd_mailbox(to_mdev(ibdev)->dev);
	if (IS_ERR(mailbox))
		return 0;

	memcpy(mailbox->buf, props->node_desc, IB_DEVICE_NODE_DESC_MAX);
	mlx4_cmd(to_mdev(ibdev)->dev, mailbox->dma, 1, 0,
		 MLX4_CMD_SET_NODE, MLX4_CMD_TIME_CLASS_A, MLX4_CMD_NATIVE);

	mlx4_free_cmd_mailbox(to_mdev(ibdev)->dev, mailbox);

	return 0;
}

static int mlx4_ib_SET_PORT(struct mlx4_ib_dev *dev, u32 port,
			    int reset_qkey_viols, u32 cap_mask)
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

	err = mlx4_cmd(dev->dev, mailbox->dma, port, MLX4_SET_PORT_IB_OPCODE,
		       MLX4_CMD_SET_PORT, MLX4_CMD_TIME_CLASS_B,
		       MLX4_CMD_WRAPPED);

	mlx4_free_cmd_mailbox(dev->dev, mailbox);
	return err;
}

static int mlx4_ib_modify_port(struct ib_device *ibdev, u32 port, int mask,
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

	err = ib_query_port(ibdev, port, &attr);
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

static int mlx4_ib_alloc_ucontext(struct ib_ucontext *uctx,
				  struct ib_udata *udata)
{
	struct ib_device *ibdev = uctx->device;
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_ib_ucontext *context = to_mucontext(uctx);
	struct mlx4_ib_alloc_ucontext_resp_v3 resp_v3;
	struct mlx4_ib_alloc_ucontext_resp resp;
	int err;

	if (!dev->ib_active)
		return -EAGAIN;

	if (ibdev->ops.uverbs_abi_ver ==
	    MLX4_IB_UVERBS_NO_DEV_CAPS_ABI_VERSION) {
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

	err = mlx4_uar_alloc(to_mdev(ibdev)->dev, &context->uar);
	if (err)
		return err;

	INIT_LIST_HEAD(&context->db_page_list);
	mutex_init(&context->db_page_mutex);

	INIT_LIST_HEAD(&context->wqn_ranges_list);
	mutex_init(&context->wqn_ranges_mutex);

	if (ibdev->ops.uverbs_abi_ver == MLX4_IB_UVERBS_NO_DEV_CAPS_ABI_VERSION)
		err = ib_copy_to_udata(udata, &resp_v3, sizeof(resp_v3));
	else
		err = ib_copy_to_udata(udata, &resp, sizeof(resp));

	if (err) {
		mlx4_uar_free(to_mdev(ibdev)->dev, &context->uar);
		return -EFAULT;
	}

	return err;
}

static void mlx4_ib_dealloc_ucontext(struct ib_ucontext *ibcontext)
{
	struct mlx4_ib_ucontext *context = to_mucontext(ibcontext);

	mlx4_uar_free(to_mdev(ibcontext->device)->dev, &context->uar);
}

static void mlx4_ib_disassociate_ucontext(struct ib_ucontext *ibcontext)
{
}

static int mlx4_ib_mmap(struct ib_ucontext *context, struct vm_area_struct *vma)
{
	struct mlx4_ib_dev *dev = to_mdev(context->device);

	switch (vma->vm_pgoff) {
	case 0:
		return rdma_user_mmap_io(context, vma,
					 to_mucontext(context)->uar.pfn,
					 PAGE_SIZE,
					 pgprot_noncached(vma->vm_page_prot),
					 NULL);

	case 1:
		if (dev->dev->caps.bf_reg_size == 0)
			return -EINVAL;
		return rdma_user_mmap_io(
			context, vma,
			to_mucontext(context)->uar.pfn +
				dev->dev->caps.num_uars,
			PAGE_SIZE, pgprot_writecombine(vma->vm_page_prot),
			NULL);

	case 3: {
		struct mlx4_clock_params params;
		int ret;

		ret = mlx4_get_internal_clock_params(dev->dev, &params);
		if (ret)
			return ret;

		return rdma_user_mmap_io(
			context, vma,
			(pci_resource_start(dev->dev->persist->pdev,
					    params.bar) +
			 params.offset) >>
				PAGE_SHIFT,
			PAGE_SIZE, pgprot_noncached(vma->vm_page_prot),
			NULL);
	}

	default:
		return -EINVAL;
	}
}

static int mlx4_ib_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct mlx4_ib_pd *pd = to_mpd(ibpd);
	struct ib_device *ibdev = ibpd->device;
	int err;

	err = mlx4_pd_alloc(to_mdev(ibdev)->dev, &pd->pdn);
	if (err)
		return err;

	if (udata && ib_copy_to_udata(udata, &pd->pdn, sizeof(__u32))) {
		mlx4_pd_free(to_mdev(ibdev)->dev, pd->pdn);
		return -EFAULT;
	}
	return 0;
}

static int mlx4_ib_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
	mlx4_pd_free(to_mdev(pd->device)->dev, to_mpd(pd)->pdn);
	return 0;
}

static int mlx4_ib_alloc_xrcd(struct ib_xrcd *ibxrcd, struct ib_udata *udata)
{
	struct mlx4_ib_dev *dev = to_mdev(ibxrcd->device);
	struct mlx4_ib_xrcd *xrcd = to_mxrcd(ibxrcd);
	struct ib_cq_init_attr cq_attr = {};
	int err;

	if (!(dev->dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC))
		return -EOPNOTSUPP;

	err = mlx4_xrcd_alloc(dev->dev, &xrcd->xrcdn);
	if (err)
		return err;

	xrcd->pd = ib_alloc_pd(ibxrcd->device, 0);
	if (IS_ERR(xrcd->pd)) {
		err = PTR_ERR(xrcd->pd);
		goto err2;
	}

	cq_attr.cqe = 1;
	xrcd->cq = ib_create_cq(ibxrcd->device, NULL, NULL, xrcd, &cq_attr);
	if (IS_ERR(xrcd->cq)) {
		err = PTR_ERR(xrcd->cq);
		goto err3;
	}

	return 0;

err3:
	ib_dealloc_pd(xrcd->pd);
err2:
	mlx4_xrcd_free(dev->dev, xrcd->xrcdn);
	return err;
}

static int mlx4_ib_dealloc_xrcd(struct ib_xrcd *xrcd, struct ib_udata *udata)
{
	ib_destroy_cq(to_mxrcd(xrcd)->cq);
	ib_dealloc_pd(to_mxrcd(xrcd)->pd);
	mlx4_xrcd_free(to_mdev(xrcd->device)->dev, to_mxrcd(xrcd)->xrcdn);
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

static void mlx4_ib_delete_counters_table(struct mlx4_ib_dev *ibdev,
					  struct mlx4_ib_counters *ctr_table)
{
	struct counter_index *counter, *tmp_count;

	mutex_lock(&ctr_table->mutex);
	list_for_each_entry_safe(counter, tmp_count, &ctr_table->counters_list,
				 list) {
		if (counter->allocated)
			mlx4_counter_free(ibdev->dev, counter->index);
		list_del(&counter->list);
		kfree(counter);
	}
	mutex_unlock(&ctr_table->mutex);
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
	struct mlx4_flow_reg_id reg_id;
	union ib_gid gid;
};

#define LAST_ETH_FIELD vlan_tag
#define LAST_IB_FIELD sl
#define LAST_IPV4_FIELD dst_ip
#define LAST_TCP_UDP_FIELD src_port

/* Field is the last supported field */
#define FIELDS_NOT_SUPPORTED(filter, field)\
	memchr_inv((void *)&filter.field  +\
		   sizeof(filter.field), 0,\
		   sizeof(filter) -\
		   offsetof(typeof(filter), field) -\
		   sizeof(filter.field))

static int parse_flow_attr(struct mlx4_dev *dev,
			   u32 qp_num,
			   union ib_flow_spec *ib_spec,
			   struct _rule_hw *mlx4_spec)
{
	enum mlx4_net_trans_rule_id type;

	switch (ib_spec->type) {
	case IB_FLOW_SPEC_ETH:
		if (FIELDS_NOT_SUPPORTED(ib_spec->eth.mask, LAST_ETH_FIELD))
			return -ENOTSUPP;

		type = MLX4_NET_TRANS_RULE_ID_ETH;
		memcpy(mlx4_spec->eth.dst_mac, ib_spec->eth.val.dst_mac,
		       ETH_ALEN);
		memcpy(mlx4_spec->eth.dst_mac_msk, ib_spec->eth.mask.dst_mac,
		       ETH_ALEN);
		mlx4_spec->eth.vlan_tag = ib_spec->eth.val.vlan_tag;
		mlx4_spec->eth.vlan_tag_msk = ib_spec->eth.mask.vlan_tag;
		break;
	case IB_FLOW_SPEC_IB:
		if (FIELDS_NOT_SUPPORTED(ib_spec->ib.mask, LAST_IB_FIELD))
			return -ENOTSUPP;

		type = MLX4_NET_TRANS_RULE_ID_IB;
		mlx4_spec->ib.l3_qpn =
			cpu_to_be32(qp_num);
		mlx4_spec->ib.qpn_mask =
			cpu_to_be32(MLX4_IB_FLOW_QPN_MASK);
		break;


	case IB_FLOW_SPEC_IPV4:
		if (FIELDS_NOT_SUPPORTED(ib_spec->ipv4.mask, LAST_IPV4_FIELD))
			return -ENOTSUPP;

		type = MLX4_NET_TRANS_RULE_ID_IPV4;
		mlx4_spec->ipv4.src_ip = ib_spec->ipv4.val.src_ip;
		mlx4_spec->ipv4.src_ip_msk = ib_spec->ipv4.mask.src_ip;
		mlx4_spec->ipv4.dst_ip = ib_spec->ipv4.val.dst_ip;
		mlx4_spec->ipv4.dst_ip_msk = ib_spec->ipv4.mask.dst_ip;
		break;

	case IB_FLOW_SPEC_TCP:
	case IB_FLOW_SPEC_UDP:
		if (FIELDS_NOT_SUPPORTED(ib_spec->tcp_udp.mask, LAST_TCP_UDP_FIELD))
			return -ENOTSUPP;

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
		union ib_flow_spec ib_spec = {};
		int ret;

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

	if (flow_attr->priority > MLX4_IB_FLOW_MAX_PRIO) {
		pr_err("Invalid priority value %d\n", flow_attr->priority);
		return -EINVAL;
	}

	if (mlx4_map_sw_to_hw_steering_mode(mdev->dev, flow_type) < 0)
		return -EINVAL;

	mailbox = mlx4_alloc_cmd_mailbox(mdev->dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);
	ctrl = mailbox->buf;

	ctrl->prio = cpu_to_be16(domain | flow_attr->priority);
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

	if (mlx4_is_master(mdev->dev) && flow_type == MLX4_FS_REGULAR &&
	    flow_attr->num_of_specs == 1) {
		struct _rule_hw *rule_header = (struct _rule_hw *)(ctrl + 1);
		enum ib_flow_spec_type header_spec =
			((union ib_flow_spec *)(flow_attr + 1))->type;

		if (header_spec == IB_FLOW_SPEC_ETH)
			mlx4_handle_eth_header_mcast_prio(ctrl, rule_header);
	}

	ret = mlx4_cmd_imm(mdev->dev, mailbox->dma, reg_id, size >> 2, 0,
			   MLX4_QP_FLOW_STEERING_ATTACH, MLX4_CMD_TIME_CLASS_A,
			   MLX4_CMD_NATIVE);
	if (ret == -ENOMEM)
		pr_err("mcg table is full. Fail to register network rule.\n");
	else if (ret == -ENXIO)
		pr_err("Device managed flow steering is disabled. Fail to register network rule.\n");
	else if (ret)
		pr_err("Invalid argument. Fail to register network rule.\n");

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

static int mlx4_ib_tunnel_steer_add(struct ib_qp *qp, struct ib_flow_attr *flow_attr,
				    u64 *reg_id)
{
	void *ib_flow;
	union ib_flow_spec *ib_spec;
	struct mlx4_dev	*dev = to_mdev(qp->device)->dev;
	int err = 0;

	if (dev->caps.tunnel_offload_mode != MLX4_TUNNEL_OFFLOAD_MODE_VXLAN ||
	    dev->caps.dmfs_high_steer_mode == MLX4_STEERING_DMFS_A0_STATIC)
		return 0; /* do nothing */

	ib_flow = flow_attr + 1;
	ib_spec = (union ib_flow_spec *)ib_flow;

	if (ib_spec->type !=  IB_FLOW_SPEC_ETH || flow_attr->num_of_specs != 1)
		return 0; /* do nothing */

	err = mlx4_tunnel_steer_add(to_mdev(qp->device)->dev, ib_spec->eth.val.dst_mac,
				    flow_attr->port, qp->qp_num,
				    MLX4_DOMAIN_UVERBS | (flow_attr->priority & 0xff),
				    reg_id);
	return err;
}

static int mlx4_ib_add_dont_trap_rule(struct mlx4_dev *dev,
				      struct ib_flow_attr *flow_attr,
				      enum mlx4_net_trans_promisc_mode *type)
{
	int err = 0;

	if (!(dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_DMFS_UC_MC_SNIFFER) ||
	    (dev->caps.dmfs_high_steer_mode == MLX4_STEERING_DMFS_A0_STATIC) ||
	    (flow_attr->num_of_specs > 1) || (flow_attr->priority != 0)) {
		return -EOPNOTSUPP;
	}

	if (flow_attr->num_of_specs == 0) {
		type[0] = MLX4_FS_MC_SNIFFER;
		type[1] = MLX4_FS_UC_SNIFFER;
	} else {
		union ib_flow_spec *ib_spec;

		ib_spec = (union ib_flow_spec *)(flow_attr + 1);
		if (ib_spec->type !=  IB_FLOW_SPEC_ETH)
			return -EINVAL;

		/* if all is zero than MC and UC */
		if (is_zero_ether_addr(ib_spec->eth.mask.dst_mac)) {
			type[0] = MLX4_FS_MC_SNIFFER;
			type[1] = MLX4_FS_UC_SNIFFER;
		} else {
			u8 mac[ETH_ALEN] = {ib_spec->eth.mask.dst_mac[0] ^ 0x01,
					    ib_spec->eth.mask.dst_mac[1],
					    ib_spec->eth.mask.dst_mac[2],
					    ib_spec->eth.mask.dst_mac[3],
					    ib_spec->eth.mask.dst_mac[4],
					    ib_spec->eth.mask.dst_mac[5]};

			/* Above xor was only on MC bit, non empty mask is valid
			 * only if this bit is set and rest are zero.
			 */
			if (!is_zero_ether_addr(&mac[0]))
				return -EINVAL;

			if (is_multicast_ether_addr(ib_spec->eth.val.dst_mac))
				type[0] = MLX4_FS_MC_SNIFFER;
			else
				type[0] = MLX4_FS_UC_SNIFFER;
		}
	}

	return err;
}

static struct ib_flow *mlx4_ib_create_flow(struct ib_qp *qp,
					   struct ib_flow_attr *flow_attr,
					   struct ib_udata *udata)
{
	int err = 0, i = 0, j = 0;
	struct mlx4_ib_flow *mflow;
	enum mlx4_net_trans_promisc_mode type[2];
	struct mlx4_dev *dev = (to_mdev(qp->device))->dev;
	int is_bonded = mlx4_is_bonded(dev);

	if (flow_attr->flags & ~IB_FLOW_ATTR_FLAGS_DONT_TRAP)
		return ERR_PTR(-EOPNOTSUPP);

	if ((flow_attr->flags & IB_FLOW_ATTR_FLAGS_DONT_TRAP) &&
	    (flow_attr->type != IB_FLOW_ATTR_NORMAL))
		return ERR_PTR(-EOPNOTSUPP);

	if (udata &&
	    udata->inlen && !ib_is_udata_cleared(udata, 0, udata->inlen))
		return ERR_PTR(-EOPNOTSUPP);

	memset(type, 0, sizeof(type));

	mflow = kzalloc(sizeof(*mflow), GFP_KERNEL);
	if (!mflow) {
		err = -ENOMEM;
		goto err_free;
	}

	switch (flow_attr->type) {
	case IB_FLOW_ATTR_NORMAL:
		/* If dont trap flag (continue match) is set, under specific
		 * condition traffic be replicated to given qp,
		 * without stealing it
		 */
		if (unlikely(flow_attr->flags & IB_FLOW_ATTR_FLAGS_DONT_TRAP)) {
			err = mlx4_ib_add_dont_trap_rule(dev,
							 flow_attr,
							 type);
			if (err)
				goto err_free;
		} else {
			type[0] = MLX4_FS_REGULAR;
		}
		break;

	case IB_FLOW_ATTR_ALL_DEFAULT:
		type[0] = MLX4_FS_ALL_DEFAULT;
		break;

	case IB_FLOW_ATTR_MC_DEFAULT:
		type[0] = MLX4_FS_MC_DEFAULT;
		break;

	case IB_FLOW_ATTR_SNIFFER:
		type[0] = MLX4_FS_MIRROR_RX_PORT;
		type[1] = MLX4_FS_MIRROR_SX_PORT;
		break;

	default:
		err = -EINVAL;
		goto err_free;
	}

	while (i < ARRAY_SIZE(type) && type[i]) {
		err = __mlx4_ib_create_flow(qp, flow_attr, MLX4_DOMAIN_UVERBS,
					    type[i], &mflow->reg_id[i].id);
		if (err)
			goto err_create_flow;
		if (is_bonded) {
			/* Application always sees one port so the mirror rule
			 * must be on port #2
			 */
			flow_attr->port = 2;
			err = __mlx4_ib_create_flow(qp, flow_attr,
						    MLX4_DOMAIN_UVERBS, type[j],
						    &mflow->reg_id[j].mirror);
			flow_attr->port = 1;
			if (err)
				goto err_create_flow;
			j++;
		}

		i++;
	}

	if (i < ARRAY_SIZE(type) && flow_attr->type == IB_FLOW_ATTR_NORMAL) {
		err = mlx4_ib_tunnel_steer_add(qp, flow_attr,
					       &mflow->reg_id[i].id);
		if (err)
			goto err_create_flow;

		if (is_bonded) {
			flow_attr->port = 2;
			err = mlx4_ib_tunnel_steer_add(qp, flow_attr,
						       &mflow->reg_id[j].mirror);
			flow_attr->port = 1;
			if (err)
				goto err_create_flow;
			j++;
		}
		/* function to create mirror rule */
		i++;
	}

	return &mflow->ibflow;

err_create_flow:
	while (i) {
		(void)__mlx4_ib_destroy_flow(to_mdev(qp->device)->dev,
					     mflow->reg_id[i].id);
		i--;
	}

	while (j) {
		(void)__mlx4_ib_destroy_flow(to_mdev(qp->device)->dev,
					     mflow->reg_id[j].mirror);
		j--;
	}
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

	while (i < ARRAY_SIZE(mflow->reg_id) && mflow->reg_id[i].id) {
		err = __mlx4_ib_destroy_flow(mdev->dev, mflow->reg_id[i].id);
		if (err)
			ret = err;
		if (mflow->reg_id[i].mirror) {
			err = __mlx4_ib_destroy_flow(mdev->dev,
						     mflow->reg_id[i].mirror);
			if (err)
				ret = err;
		}
		i++;
	}

	kfree(mflow);
	return ret;
}

static int mlx4_ib_mcg_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	int err;
	struct mlx4_ib_dev *mdev = to_mdev(ibqp->device);
	struct mlx4_dev	*dev = mdev->dev;
	struct mlx4_ib_qp *mqp = to_mqp(ibqp);
	struct mlx4_ib_steering *ib_steering = NULL;
	enum mlx4_protocol prot = MLX4_PROT_IB_IPV6;
	struct mlx4_flow_reg_id	reg_id;

	if (mdev->dev->caps.steering_mode ==
	    MLX4_STEERING_MODE_DEVICE_MANAGED) {
		ib_steering = kmalloc(sizeof(*ib_steering), GFP_KERNEL);
		if (!ib_steering)
			return -ENOMEM;
	}

	err = mlx4_multicast_attach(mdev->dev, &mqp->mqp, gid->raw, mqp->port,
				    !!(mqp->flags &
				       MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK),
				    prot, &reg_id.id);
	if (err) {
		pr_err("multicast attach op failed, err %d\n", err);
		goto err_malloc;
	}

	reg_id.mirror = 0;
	if (mlx4_is_bonded(dev)) {
		err = mlx4_multicast_attach(mdev->dev, &mqp->mqp, gid->raw,
					    (mqp->port == 1) ? 2 : 1,
					    !!(mqp->flags &
					    MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK),
					    prot, &reg_id.mirror);
		if (err)
			goto err_add;
	}

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
			      prot, reg_id.id);
	if (reg_id.mirror)
		mlx4_multicast_detach(mdev->dev, &mqp->mqp, gid->raw,
				      prot, reg_id.mirror);
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
	struct mlx4_dev *dev = mdev->dev;
	struct mlx4_ib_qp *mqp = to_mqp(ibqp);
	struct net_device *ndev;
	struct mlx4_ib_gid_entry *ge;
	struct mlx4_flow_reg_id reg_id = {0, 0};
	enum mlx4_protocol prot =  MLX4_PROT_IB_IPV6;

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
				    prot, reg_id.id);
	if (err)
		return err;

	if (mlx4_is_bonded(dev)) {
		err = mlx4_multicast_detach(mdev->dev, &mqp->mqp, gid->raw,
					    prot, reg_id.mirror);
		if (err)
			return err;
	}

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

	memcpy(dev->ib_dev.node_desc, out_mad->data, IB_DEVICE_NODE_DESC_MAX);

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

static ssize_t hca_type_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct mlx4_ib_dev *dev =
		rdma_device_to_drv_device(device, struct mlx4_ib_dev, ib_dev);

	return sysfs_emit(buf, "MT%d\n", dev->dev->persist->pdev->device);
}
static DEVICE_ATTR_RO(hca_type);

static ssize_t hw_rev_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	struct mlx4_ib_dev *dev =
		rdma_device_to_drv_device(device, struct mlx4_ib_dev, ib_dev);

	return sysfs_emit(buf, "%x\n", dev->dev->rev_id);
}
static DEVICE_ATTR_RO(hw_rev);

static ssize_t board_id_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct mlx4_ib_dev *dev =
		rdma_device_to_drv_device(device, struct mlx4_ib_dev, ib_dev);

	return sysfs_emit(buf, "%.*s\n", MLX4_BOARD_ID_LEN, dev->dev->board_id);
}
static DEVICE_ATTR_RO(board_id);

static struct attribute *mlx4_class_attributes[] = {
	&dev_attr_hw_rev.attr,
	&dev_attr_hca_type.attr,
	&dev_attr_board_id.attr,
	NULL
};

static const struct attribute_group mlx4_attr_group = {
	.attrs = mlx4_class_attributes,
};

struct diag_counter {
	const char *name;
	u32 offset;
};

#define DIAG_COUNTER(_name, _offset)			\
	{ .name = #_name, .offset = _offset }

static const struct diag_counter diag_basic[] = {
	DIAG_COUNTER(rq_num_lle, 0x00),
	DIAG_COUNTER(sq_num_lle, 0x04),
	DIAG_COUNTER(rq_num_lqpoe, 0x08),
	DIAG_COUNTER(sq_num_lqpoe, 0x0C),
	DIAG_COUNTER(rq_num_lpe, 0x18),
	DIAG_COUNTER(sq_num_lpe, 0x1C),
	DIAG_COUNTER(rq_num_wrfe, 0x20),
	DIAG_COUNTER(sq_num_wrfe, 0x24),
	DIAG_COUNTER(sq_num_mwbe, 0x2C),
	DIAG_COUNTER(sq_num_bre, 0x34),
	DIAG_COUNTER(sq_num_rire, 0x44),
	DIAG_COUNTER(rq_num_rire, 0x48),
	DIAG_COUNTER(sq_num_rae, 0x4C),
	DIAG_COUNTER(rq_num_rae, 0x50),
	DIAG_COUNTER(sq_num_roe, 0x54),
	DIAG_COUNTER(sq_num_tree, 0x5C),
	DIAG_COUNTER(sq_num_rree, 0x64),
	DIAG_COUNTER(rq_num_rnr, 0x68),
	DIAG_COUNTER(sq_num_rnr, 0x6C),
	DIAG_COUNTER(rq_num_oos, 0x100),
	DIAG_COUNTER(sq_num_oos, 0x104),
};

static const struct diag_counter diag_ext[] = {
	DIAG_COUNTER(rq_num_dup, 0x130),
	DIAG_COUNTER(sq_num_to, 0x134),
};

static const struct diag_counter diag_device_only[] = {
	DIAG_COUNTER(num_cqovf, 0x1A0),
	DIAG_COUNTER(rq_num_udsdprd, 0x118),
};

static struct rdma_hw_stats *
mlx4_ib_alloc_hw_device_stats(struct ib_device *ibdev)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_ib_diag_counters *diag = dev->diag_counters;

	if (!diag[0].descs)
		return NULL;

	return rdma_alloc_hw_stats_struct(diag[0].descs, diag[0].num_counters,
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

static struct rdma_hw_stats *
mlx4_ib_alloc_hw_port_stats(struct ib_device *ibdev, u32 port_num)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_ib_diag_counters *diag = dev->diag_counters;

	if (!diag[1].descs)
		return NULL;

	return rdma_alloc_hw_stats_struct(diag[1].descs, diag[1].num_counters,
					  RDMA_HW_STATS_DEFAULT_LIFESPAN);
}

static int mlx4_ib_get_hw_stats(struct ib_device *ibdev,
				struct rdma_hw_stats *stats,
				u32 port, int index)
{
	struct mlx4_ib_dev *dev = to_mdev(ibdev);
	struct mlx4_ib_diag_counters *diag = dev->diag_counters;
	u32 hw_value[ARRAY_SIZE(diag_device_only) +
		ARRAY_SIZE(diag_ext) + ARRAY_SIZE(diag_basic)] = {};
	int ret;
	int i;

	ret = mlx4_query_diag_counters(dev->dev,
				       MLX4_OP_MOD_QUERY_TRANSPORT_CI_ERRORS,
				       diag[!!port].offset, hw_value,
				       diag[!!port].num_counters, port);

	if (ret)
		return ret;

	for (i = 0; i < diag[!!port].num_counters; i++)
		stats->value[i] = hw_value[i];

	return diag[!!port].num_counters;
}

static int __mlx4_ib_alloc_diag_counters(struct mlx4_ib_dev *ibdev,
					 struct rdma_stat_desc **pdescs,
					 u32 **offset, u32 *num, bool port)
{
	u32 num_counters;

	num_counters = ARRAY_SIZE(diag_basic);

	if (ibdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_DIAG_PER_PORT)
		num_counters += ARRAY_SIZE(diag_ext);

	if (!port)
		num_counters += ARRAY_SIZE(diag_device_only);

	*pdescs = kcalloc(num_counters, sizeof(struct rdma_stat_desc),
			  GFP_KERNEL);
	if (!*pdescs)
		return -ENOMEM;

	*offset = kcalloc(num_counters, sizeof(**offset), GFP_KERNEL);
	if (!*offset)
		goto err;

	*num = num_counters;

	return 0;

err:
	kfree(*pdescs);
	return -ENOMEM;
}

static void mlx4_ib_fill_diag_counters(struct mlx4_ib_dev *ibdev,
				       struct rdma_stat_desc *descs,
				       u32 *offset, bool port)
{
	int i;
	int j;

	for (i = 0, j = 0; i < ARRAY_SIZE(diag_basic); i++, j++) {
		descs[i].name = diag_basic[i].name;
		offset[i] = diag_basic[i].offset;
	}

	if (ibdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_DIAG_PER_PORT) {
		for (i = 0; i < ARRAY_SIZE(diag_ext); i++, j++) {
			descs[j].name = diag_ext[i].name;
			offset[j] = diag_ext[i].offset;
		}
	}

	if (!port) {
		for (i = 0; i < ARRAY_SIZE(diag_device_only); i++, j++) {
			descs[j].name = diag_device_only[i].name;
			offset[j] = diag_device_only[i].offset;
		}
	}
}

static const struct ib_device_ops mlx4_ib_hw_stats_ops = {
	.alloc_hw_device_stats = mlx4_ib_alloc_hw_device_stats,
	.alloc_hw_port_stats = mlx4_ib_alloc_hw_port_stats,
	.get_hw_stats = mlx4_ib_get_hw_stats,
};

static int mlx4_ib_alloc_diag_counters(struct mlx4_ib_dev *ibdev)
{
	struct mlx4_ib_diag_counters *diag = ibdev->diag_counters;
	int i;
	int ret;
	bool per_port = !!(ibdev->dev->caps.flags2 &
		MLX4_DEV_CAP_FLAG2_DIAG_PER_PORT);

	if (mlx4_is_slave(ibdev->dev))
		return 0;

	for (i = 0; i < MLX4_DIAG_COUNTERS_TYPES; i++) {
		/* i == 1 means we are building port counters */
		if (i && !per_port)
			continue;

		ret = __mlx4_ib_alloc_diag_counters(ibdev, &diag[i].descs,
						    &diag[i].offset,
						    &diag[i].num_counters, i);
		if (ret)
			goto err_alloc;

		mlx4_ib_fill_diag_counters(ibdev, diag[i].descs,
					   diag[i].offset, i);
	}

	ib_set_device_ops(&ibdev->ib_dev, &mlx4_ib_hw_stats_ops);

	return 0;

err_alloc:
	if (i) {
		kfree(diag[i - 1].descs);
		kfree(diag[i - 1].offset);
	}

	return ret;
}

static void mlx4_ib_diag_cleanup(struct mlx4_ib_dev *ibdev)
{
	int i;

	for (i = 0; i < MLX4_DIAG_COUNTERS_TYPES; i++) {
		kfree(ibdev->diag_counters[i].offset);
		kfree(ibdev->diag_counters[i].descs);
	}
}

#define MLX4_IB_INVALID_MAC	((u64)-1)
static void mlx4_ib_update_qps(struct mlx4_ib_dev *ibdev,
			       struct net_device *dev,
			       int port)
{
	u64 new_smac = 0;
	u64 release_mac = MLX4_IB_INVALID_MAC;
	struct mlx4_ib_qp *qp;

	new_smac = mlx4_mac_to_u64(dev->dev_addr);
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
		if (mlx4_update_qp(ibdev->dev, qp->mqp.qpn, MLX4_UPDATE_QP_SMAC,
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

static void mlx4_ib_scan_netdevs(struct mlx4_ib_dev *ibdev,
				 struct net_device *dev,
				 unsigned long event)

{
	struct mlx4_ib_iboe *iboe;
	int update_qps_port = -1;
	int port;

	ASSERT_RTNL();

	iboe = &ibdev->iboe;

	spin_lock_bh(&iboe->lock);
	mlx4_foreach_ib_transport_port(port, ibdev->dev) {

		iboe->netdevs[port - 1] =
			mlx4_get_protocol_dev(ibdev->dev, MLX4_PROT_ETH, port);

		if (dev == iboe->netdevs[port - 1] &&
		    (event == NETDEV_CHANGEADDR || event == NETDEV_REGISTER ||
		     event == NETDEV_UP || event == NETDEV_CHANGE))
			update_qps_port = port;

		if (dev == iboe->netdevs[port - 1] &&
		    (event == NETDEV_UP || event == NETDEV_DOWN)) {
			enum ib_port_state port_state;
			struct ib_event ibev = { };

			if (ib_get_cached_port_state(&ibdev->ib_dev, port,
						     &port_state))
				continue;

			if (event == NETDEV_UP &&
			    (port_state != IB_PORT_ACTIVE ||
			     iboe->last_port_state[port - 1] != IB_PORT_DOWN))
				continue;
			if (event == NETDEV_DOWN &&
			    (port_state != IB_PORT_DOWN ||
			     iboe->last_port_state[port - 1] != IB_PORT_ACTIVE))
				continue;
			iboe->last_port_state[port - 1] = port_state;

			ibev.device = &ibdev->ib_dev;
			ibev.element.port_num = port;
			ibev.event = event == NETDEV_UP ? IB_EVENT_PORT_ACTIVE :
							  IB_EVENT_PORT_ERR;
			ib_dispatch_event(&ibev);
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
		for (slave = 0; slave <= ibdev->dev->persist->num_vfs;
		     ++slave) {
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
	int i, j, eq = 0, total_eqs = 0;

	ibdev->eq_table = kcalloc(dev->caps.num_comp_vectors,
				  sizeof(ibdev->eq_table[0]), GFP_KERNEL);
	if (!ibdev->eq_table)
		return;

	for (i = 1; i <= dev->caps.num_ports; i++) {
		for (j = 0; j < mlx4_get_eqs_per_port(dev, i);
		     j++, total_eqs++) {
			if (i > 1 &&  mlx4_is_eq_shared(dev, total_eqs))
				continue;
			ibdev->eq_table[eq] = total_eqs;
			if (!mlx4_assign_eq(dev, i,
					    &ibdev->eq_table[eq]))
				eq++;
			else
				ibdev->eq_table[eq] = -1;
		}
	}

	for (i = eq; i < dev->caps.num_comp_vectors;
	     ibdev->eq_table[i++] = -1)
		;

	/* Advertise the new number of EQs to clients */
	ibdev->ib_dev.num_comp_vectors = eq;
}

static void mlx4_ib_free_eqs(struct mlx4_dev *dev, struct mlx4_ib_dev *ibdev)
{
	int i;
	int total_eqs = ibdev->ib_dev.num_comp_vectors;

	/* no eqs were allocated */
	if (!ibdev->eq_table)
		return;

	/* Reset the advertised EQ number */
	ibdev->ib_dev.num_comp_vectors = 0;

	for (i = 0; i < total_eqs; i++)
		mlx4_release_eq(dev, ibdev->eq_table[i]);

	kfree(ibdev->eq_table);
	ibdev->eq_table = NULL;
}

static int mlx4_port_immutable(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	struct mlx4_ib_dev *mdev = to_mdev(ibdev);
	int err;

	if (mlx4_ib_port_link_layer(ibdev, port_num) == IB_LINK_LAYER_INFINIBAND) {
		immutable->core_cap_flags = RDMA_CORE_PORT_IBA_IB;
		immutable->max_mad_size = IB_MGMT_MAD_SIZE;
	} else {
		if (mdev->dev->caps.flags & MLX4_DEV_CAP_FLAG_IBOE)
			immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE;
		if (mdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ROCE_V1_V2)
			immutable->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE |
				RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;
		immutable->core_cap_flags |= RDMA_CORE_PORT_RAW_PACKET;
		if (immutable->core_cap_flags & (RDMA_CORE_PORT_IBA_ROCE |
		    RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP))
			immutable->max_mad_size = IB_MGMT_MAD_SIZE;
	}

	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;

	return 0;
}

static void get_fw_ver_str(struct ib_device *device, char *str)
{
	struct mlx4_ib_dev *dev =
		container_of(device, struct mlx4_ib_dev, ib_dev);
	snprintf(str, IB_FW_VERSION_NAME_MAX, "%d.%d.%d",
		 (int) (dev->dev->caps.fw_ver >> 32),
		 (int) (dev->dev->caps.fw_ver >> 16) & 0xffff,
		 (int) dev->dev->caps.fw_ver & 0xffff);
}

static const struct ib_device_ops mlx4_ib_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_MLX4,
	.uverbs_abi_ver = MLX4_IB_UVERBS_ABI_VERSION,

	.add_gid = mlx4_ib_add_gid,
	.alloc_mr = mlx4_ib_alloc_mr,
	.alloc_pd = mlx4_ib_alloc_pd,
	.alloc_ucontext = mlx4_ib_alloc_ucontext,
	.attach_mcast = mlx4_ib_mcg_attach,
	.create_ah = mlx4_ib_create_ah,
	.create_cq = mlx4_ib_create_cq,
	.create_qp = mlx4_ib_create_qp,
	.create_srq = mlx4_ib_create_srq,
	.dealloc_pd = mlx4_ib_dealloc_pd,
	.dealloc_ucontext = mlx4_ib_dealloc_ucontext,
	.del_gid = mlx4_ib_del_gid,
	.dereg_mr = mlx4_ib_dereg_mr,
	.destroy_ah = mlx4_ib_destroy_ah,
	.destroy_cq = mlx4_ib_destroy_cq,
	.destroy_qp = mlx4_ib_destroy_qp,
	.destroy_srq = mlx4_ib_destroy_srq,
	.detach_mcast = mlx4_ib_mcg_detach,
	.device_group = &mlx4_attr_group,
	.disassociate_ucontext = mlx4_ib_disassociate_ucontext,
	.drain_rq = mlx4_ib_drain_rq,
	.drain_sq = mlx4_ib_drain_sq,
	.get_dev_fw_str = get_fw_ver_str,
	.get_dma_mr = mlx4_ib_get_dma_mr,
	.get_link_layer = mlx4_ib_port_link_layer,
	.get_netdev = mlx4_ib_get_netdev,
	.get_port_immutable = mlx4_port_immutable,
	.map_mr_sg = mlx4_ib_map_mr_sg,
	.mmap = mlx4_ib_mmap,
	.modify_cq = mlx4_ib_modify_cq,
	.modify_device = mlx4_ib_modify_device,
	.modify_port = mlx4_ib_modify_port,
	.modify_qp = mlx4_ib_modify_qp,
	.modify_srq = mlx4_ib_modify_srq,
	.poll_cq = mlx4_ib_poll_cq,
	.post_recv = mlx4_ib_post_recv,
	.post_send = mlx4_ib_post_send,
	.post_srq_recv = mlx4_ib_post_srq_recv,
	.process_mad = mlx4_ib_process_mad,
	.query_ah = mlx4_ib_query_ah,
	.query_device = mlx4_ib_query_device,
	.query_gid = mlx4_ib_query_gid,
	.query_pkey = mlx4_ib_query_pkey,
	.query_port = mlx4_ib_query_port,
	.query_qp = mlx4_ib_query_qp,
	.query_srq = mlx4_ib_query_srq,
	.reg_user_mr = mlx4_ib_reg_user_mr,
	.req_notify_cq = mlx4_ib_arm_cq,
	.rereg_user_mr = mlx4_ib_rereg_user_mr,
	.resize_cq = mlx4_ib_resize_cq,

	INIT_RDMA_OBJ_SIZE(ib_ah, mlx4_ib_ah, ibah),
	INIT_RDMA_OBJ_SIZE(ib_cq, mlx4_ib_cq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_pd, mlx4_ib_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_qp, mlx4_ib_qp, ibqp),
	INIT_RDMA_OBJ_SIZE(ib_srq, mlx4_ib_srq, ibsrq),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, mlx4_ib_ucontext, ibucontext),
};

static const struct ib_device_ops mlx4_ib_dev_wq_ops = {
	.create_rwq_ind_table = mlx4_ib_create_rwq_ind_table,
	.create_wq = mlx4_ib_create_wq,
	.destroy_rwq_ind_table = mlx4_ib_destroy_rwq_ind_table,
	.destroy_wq = mlx4_ib_destroy_wq,
	.modify_wq = mlx4_ib_modify_wq,

	INIT_RDMA_OBJ_SIZE(ib_rwq_ind_table, mlx4_ib_rwq_ind_table,
			   ib_rwq_ind_tbl),
};

static const struct ib_device_ops mlx4_ib_dev_mw_ops = {
	.alloc_mw = mlx4_ib_alloc_mw,
	.dealloc_mw = mlx4_ib_dealloc_mw,

	INIT_RDMA_OBJ_SIZE(ib_mw, mlx4_ib_mw, ibmw),
};

static const struct ib_device_ops mlx4_ib_dev_xrc_ops = {
	.alloc_xrcd = mlx4_ib_alloc_xrcd,
	.dealloc_xrcd = mlx4_ib_dealloc_xrcd,

	INIT_RDMA_OBJ_SIZE(ib_xrcd, mlx4_ib_xrcd, ibxrcd),
};

static const struct ib_device_ops mlx4_ib_dev_fs_ops = {
	.create_flow = mlx4_ib_create_flow,
	.destroy_flow = mlx4_ib_destroy_flow,
};

static void *mlx4_ib_add(struct mlx4_dev *dev)
{
	struct mlx4_ib_dev *ibdev;
	int num_ports = 0;
	int i, j;
	int err;
	struct mlx4_ib_iboe *iboe;
	int ib_num_ports = 0;
	int num_req_counters;
	int allocated;
	u32 counter_index;
	struct counter_index *new_counter_index = NULL;

	pr_info_once("%s", mlx4_ib_version);

	num_ports = 0;
	mlx4_foreach_ib_transport_port(i, dev)
		num_ports++;

	/* No point in registering a device with no ports... */
	if (num_ports == 0)
		return NULL;

	ibdev = ib_alloc_device(mlx4_ib_dev, ib_dev);
	if (!ibdev) {
		dev_err(&dev->persist->pdev->dev,
			"Device struct alloc failed\n");
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
	ibdev->bond_next_port	= 0;

	ibdev->ib_dev.node_type		= RDMA_NODE_IB_CA;
	ibdev->ib_dev.local_dma_lkey	= dev->caps.reserved_lkey;
	ibdev->num_ports		= num_ports;
	ibdev->ib_dev.phys_port_cnt     = mlx4_is_bonded(dev) ?
						1 : ibdev->num_ports;
	ibdev->ib_dev.num_comp_vectors	= dev->caps.num_comp_vectors;
	ibdev->ib_dev.dev.parent	= &dev->persist->pdev->dev;

	ib_set_device_ops(&ibdev->ib_dev, &mlx4_ib_dev_ops);

	if ((dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_RSS) &&
	    ((mlx4_ib_port_link_layer(&ibdev->ib_dev, 1) ==
	    IB_LINK_LAYER_ETHERNET) ||
	    (mlx4_ib_port_link_layer(&ibdev->ib_dev, 2) ==
	    IB_LINK_LAYER_ETHERNET)))
		ib_set_device_ops(&ibdev->ib_dev, &mlx4_ib_dev_wq_ops);

	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_MEM_WINDOW ||
	    dev->caps.bmme_flags & MLX4_BMME_FLAG_TYPE_2_WIN)
		ib_set_device_ops(&ibdev->ib_dev, &mlx4_ib_dev_mw_ops);

	if (dev->caps.flags & MLX4_DEV_CAP_FLAG_XRC) {
		ib_set_device_ops(&ibdev->ib_dev, &mlx4_ib_dev_xrc_ops);
	}

	if (check_flow_steering_support(dev)) {
		ibdev->steering_support = MLX4_STEERING_MODE_DEVICE_MANAGED;
		ib_set_device_ops(&ibdev->ib_dev, &mlx4_ib_dev_fs_ops);
	}

	if (!dev->caps.userspace_caps)
		ibdev->ib_dev.ops.uverbs_abi_ver =
			MLX4_IB_UVERBS_NO_DEV_CAPS_ABI_VERSION;

	mlx4_ib_alloc_eqs(dev, ibdev);

	spin_lock_init(&iboe->lock);

	if (init_node_data(ibdev))
		goto err_map;
	mlx4_init_sl2vl_tbl(ibdev);

	for (i = 0; i < ibdev->num_ports; ++i) {
		mutex_init(&ibdev->counters_table[i].mutex);
		INIT_LIST_HEAD(&ibdev->counters_table[i].counters_list);
		iboe->last_port_state[i] = IB_PORT_DOWN;
	}

	num_req_counters = mlx4_is_bonded(dev) ? 1 : ibdev->num_ports;
	for (i = 0; i < num_req_counters; ++i) {
		mutex_init(&ibdev->qp1_proxy_lock[i]);
		allocated = 0;
		if (mlx4_ib_port_link_layer(&ibdev->ib_dev, i + 1) ==
						IB_LINK_LAYER_ETHERNET) {
			err = mlx4_counter_alloc(ibdev->dev, &counter_index,
						 MLX4_RES_USAGE_DRIVER);
			/* if failed to allocate a new counter, use default */
			if (err)
				counter_index =
					mlx4_get_default_counter_index(dev,
								       i + 1);
			else
				allocated = 1;
		} else { /* IB_LINK_LAYER_INFINIBAND use the default counter */
			counter_index = mlx4_get_default_counter_index(dev,
								       i + 1);
		}
		new_counter_index = kmalloc(sizeof(*new_counter_index),
					    GFP_KERNEL);
		if (!new_counter_index) {
			if (allocated)
				mlx4_counter_free(ibdev->dev, counter_index);
			goto err_counter;
		}
		new_counter_index->index = counter_index;
		new_counter_index->allocated = allocated;
		list_add_tail(&new_counter_index->list,
			      &ibdev->counters_table[i].counters_list);
		ibdev->counters_table[i].default_counter = counter_index;
		pr_info("counter index %d for port %d allocated %d\n",
			counter_index, i + 1, allocated);
	}
	if (mlx4_is_bonded(dev))
		for (i = 1; i < ibdev->num_ports ; ++i) {
			new_counter_index =
					kmalloc(sizeof(struct counter_index),
						GFP_KERNEL);
			if (!new_counter_index)
				goto err_counter;
			new_counter_index->index = counter_index;
			new_counter_index->allocated = 0;
			list_add_tail(&new_counter_index->list,
				      &ibdev->counters_table[i].counters_list);
			ibdev->counters_table[i].default_counter =
								counter_index;
		}

	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_IB)
		ib_num_ports++;

	spin_lock_init(&ibdev->sm_lock);
	mutex_init(&ibdev->cap_mask_mutex);
	INIT_LIST_HEAD(&ibdev->qp_list);
	spin_lock_init(&ibdev->reset_flow_resource_lock);

	if (ibdev->steering_support == MLX4_STEERING_MODE_DEVICE_MANAGED &&
	    ib_num_ports) {
		ibdev->steer_qpn_count = MLX4_IB_UC_MAX_NUM_QPS;
		err = mlx4_qp_reserve_range(dev, ibdev->steer_qpn_count,
					    MLX4_IB_UC_STEER_QPN_ALIGN,
					    &ibdev->steer_qpn_base, 0,
					    MLX4_RES_USAGE_DRIVER);
		if (err)
			goto err_counter;

		ibdev->ib_uc_qpns_bitmap =
			kmalloc_array(BITS_TO_LONGS(ibdev->steer_qpn_count),
				      sizeof(long),
				      GFP_KERNEL);
		if (!ibdev->ib_uc_qpns_bitmap)
			goto err_steer_qp_release;

		if (dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_DMFS_IPOIB) {
			bitmap_zero(ibdev->ib_uc_qpns_bitmap,
				    ibdev->steer_qpn_count);
			err = mlx4_FLOW_STEERING_IB_UC_QP_RANGE(
					dev, ibdev->steer_qpn_base,
					ibdev->steer_qpn_base +
					ibdev->steer_qpn_count - 1);
			if (err)
				goto err_steer_free_bitmap;
		} else {
			bitmap_fill(ibdev->ib_uc_qpns_bitmap,
				    ibdev->steer_qpn_count);
		}
	}

	for (j = 1; j <= ibdev->dev->caps.num_ports; j++)
		atomic64_set(&iboe->mac[j - 1], ibdev->dev->caps.def_mac[j]);

	if (mlx4_ib_alloc_diag_counters(ibdev))
		goto err_steer_free_bitmap;

	if (ib_register_device(&ibdev->ib_dev, "mlx4_%d",
			       &dev->persist->pdev->dev))
		goto err_diag_counters;

	if (mlx4_ib_mad_init(ibdev))
		goto err_reg;

	if (mlx4_ib_init_sriov(ibdev))
		goto err_mad;

	if (!iboe->nb.notifier_call) {
		iboe->nb.notifier_call = mlx4_ib_netdev_event;
		err = register_netdevice_notifier(&iboe->nb);
		if (err) {
			iboe->nb.notifier_call = NULL;
			goto err_notif;
		}
	}
	if (dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_ROCE_V1_V2) {
		err = mlx4_config_roce_v2_port(dev, ROCE_V2_UDP_DPORT);
		if (err)
			goto err_notif;
	}

	ibdev->ib_active = true;
	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_IB)
		devlink_port_type_ib_set(mlx4_get_devlink_port(dev, i),
					 &ibdev->ib_dev);

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
	flush_workqueue(wq);

	mlx4_ib_close_sriov(ibdev);

err_mad:
	mlx4_ib_mad_cleanup(ibdev);

err_reg:
	ib_unregister_device(&ibdev->ib_dev);

err_diag_counters:
	mlx4_ib_diag_cleanup(ibdev);

err_steer_free_bitmap:
	kfree(ibdev->ib_uc_qpns_bitmap);

err_steer_qp_release:
	mlx4_qp_release_range(dev, ibdev->steer_qpn_base,
			      ibdev->steer_qpn_count);
err_counter:
	for (i = 0; i < ibdev->num_ports; ++i)
		mlx4_ib_delete_counters_table(ibdev, &ibdev->counters_table[i]);

err_map:
	mlx4_ib_free_eqs(dev, ibdev);
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

	if (WARN(qpn < dev->steer_qpn_base, "qpn = %u, steer_qpn_base = %u\n",
		 qpn, dev->steer_qpn_base))
		/* not supposed to be here */
		return;

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

		err = __mlx4_ib_create_flow(&mqp->ibqp, flow, MLX4_DOMAIN_NIC,
					    MLX4_FS_REGULAR, &mqp->reg_id);
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
	int i;

	mlx4_foreach_port(i, dev, MLX4_PORT_TYPE_IB)
		devlink_port_type_clear(mlx4_get_devlink_port(dev, i));
	ibdev->ib_active = false;
	flush_workqueue(wq);

	if (ibdev->iboe.nb.notifier_call) {
		if (unregister_netdevice_notifier(&ibdev->iboe.nb))
			pr_warn("failure unregistering notifier\n");
		ibdev->iboe.nb.notifier_call = NULL;
	}

	mlx4_ib_close_sriov(ibdev);
	mlx4_ib_mad_cleanup(ibdev);
	ib_unregister_device(&ibdev->ib_dev);
	mlx4_ib_diag_cleanup(ibdev);

	mlx4_qp_release_range(dev, ibdev->steer_qpn_base,
			      ibdev->steer_qpn_count);
	kfree(ibdev->ib_uc_qpns_bitmap);

	iounmap(ibdev->uar_map);
	for (p = 0; p < ibdev->num_ports; ++p)
		mlx4_ib_delete_counters_table(ibdev, &ibdev->counters_table[p]);

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
	if (!dm)
		return;

	for (i = 0; i < ports; i++) {
		dm[i] = kmalloc(sizeof (struct mlx4_ib_demux_work), GFP_ATOMIC);
		if (!dm[i]) {
			while (--i >= 0)
				kfree(dm[i]);
			goto out;
		}
		INIT_WORK(&dm[i]->work, mlx4_ib_tunnels_update_work);
		dm[i]->port = first_port + i + 1;
		dm[i]->slave = slave;
		dm[i]->do_init = do_init;
		dm[i]->dev = ibdev;
	}
	/* initialize or tear down tunnel QPs for the slave */
	spin_lock_irqsave(&ibdev->sriov.going_down_lock, flags);
	if (!ibdev->sriov.is_going_down) {
		for (i = 0; i < ports; i++)
			queue_work(ibdev->sriov.demux[i].ud_wq, &dm[i]->work);
		spin_unlock_irqrestore(&ibdev->sriov.going_down_lock, flags);
	} else {
		spin_unlock_irqrestore(&ibdev->sriov.going_down_lock, flags);
		for (i = 0; i < ports; i++)
			kfree(dm[i]);
	}
out:
	kfree(dm);
	return;
}

static void mlx4_ib_handle_catas_error(struct mlx4_ib_dev *ibdev)
{
	struct mlx4_ib_qp *mqp;
	unsigned long flags_qp;
	unsigned long flags_cq;
	struct mlx4_ib_cq *send_mcq, *recv_mcq;
	struct list_head    cq_notify_list;
	struct mlx4_cq *mcq;
	unsigned long flags;

	pr_warn("mlx4_ib_handle_catas_error was started\n");
	INIT_LIST_HEAD(&cq_notify_list);

	/* Go over qp list reside on that ibdev, sync with create/destroy qp.*/
	spin_lock_irqsave(&ibdev->reset_flow_resource_lock, flags);

	list_for_each_entry(mqp, &ibdev->qp_list, qps_list) {
		spin_lock_irqsave(&mqp->sq.lock, flags_qp);
		if (mqp->sq.tail != mqp->sq.head) {
			send_mcq = to_mcq(mqp->ibqp.send_cq);
			spin_lock_irqsave(&send_mcq->lock, flags_cq);
			if (send_mcq->mcq.comp &&
			    mqp->ibqp.send_cq->comp_handler) {
				if (!send_mcq->mcq.reset_notify_added) {
					send_mcq->mcq.reset_notify_added = 1;
					list_add_tail(&send_mcq->mcq.reset_notify,
						      &cq_notify_list);
				}
			}
			spin_unlock_irqrestore(&send_mcq->lock, flags_cq);
		}
		spin_unlock_irqrestore(&mqp->sq.lock, flags_qp);
		/* Now, handle the QP's receive queue */
		spin_lock_irqsave(&mqp->rq.lock, flags_qp);
		/* no handling is needed for SRQ */
		if (!mqp->ibqp.srq) {
			if (mqp->rq.tail != mqp->rq.head) {
				recv_mcq = to_mcq(mqp->ibqp.recv_cq);
				spin_lock_irqsave(&recv_mcq->lock, flags_cq);
				if (recv_mcq->mcq.comp &&
				    mqp->ibqp.recv_cq->comp_handler) {
					if (!recv_mcq->mcq.reset_notify_added) {
						recv_mcq->mcq.reset_notify_added = 1;
						list_add_tail(&recv_mcq->mcq.reset_notify,
							      &cq_notify_list);
					}
				}
				spin_unlock_irqrestore(&recv_mcq->lock,
						       flags_cq);
			}
		}
		spin_unlock_irqrestore(&mqp->rq.lock, flags_qp);
	}

	list_for_each_entry(mcq, &cq_notify_list, reset_notify) {
		mcq->comp(mcq);
	}
	spin_unlock_irqrestore(&ibdev->reset_flow_resource_lock, flags);
	pr_warn("mlx4_ib_handle_catas_error ended\n");
}

static void handle_bonded_port_state_event(struct work_struct *work)
{
	struct ib_event_work *ew =
		container_of(work, struct ib_event_work, work);
	struct mlx4_ib_dev *ibdev = ew->ib_dev;
	enum ib_port_state bonded_port_state = IB_PORT_NOP;
	int i;
	struct ib_event ibev;

	kfree(ew);
	spin_lock_bh(&ibdev->iboe.lock);
	for (i = 0; i < MLX4_MAX_PORTS; ++i) {
		struct net_device *curr_netdev = ibdev->iboe.netdevs[i];
		enum ib_port_state curr_port_state;

		if (!curr_netdev)
			continue;

		curr_port_state =
			(netif_running(curr_netdev) &&
			 netif_carrier_ok(curr_netdev)) ?
			IB_PORT_ACTIVE : IB_PORT_DOWN;

		bonded_port_state = (bonded_port_state != IB_PORT_ACTIVE) ?
			curr_port_state : IB_PORT_ACTIVE;
	}
	spin_unlock_bh(&ibdev->iboe.lock);

	ibev.device = &ibdev->ib_dev;
	ibev.element.port_num = 1;
	ibev.event = (bonded_port_state == IB_PORT_ACTIVE) ?
		IB_EVENT_PORT_ACTIVE : IB_EVENT_PORT_ERR;

	ib_dispatch_event(&ibev);
}

void mlx4_ib_sl2vl_update(struct mlx4_ib_dev *mdev, int port)
{
	u64 sl2vl;
	int err;

	err = mlx4_ib_query_sl2vl(&mdev->ib_dev, port, &sl2vl);
	if (err) {
		pr_err("Unable to get current sl to vl mapping for port %d.  Using all zeroes (%d)\n",
		       port, err);
		sl2vl = 0;
	}
	atomic64_set(&mdev->sl2vl[port - 1], sl2vl);
}

static void ib_sl2vl_update_work(struct work_struct *work)
{
	struct ib_event_work *ew = container_of(work, struct ib_event_work, work);
	struct mlx4_ib_dev *mdev = ew->ib_dev;
	int port = ew->port;

	mlx4_ib_sl2vl_update(mdev, port);

	kfree(ew);
}

void mlx4_sched_ib_sl2vl_update_work(struct mlx4_ib_dev *ibdev,
				     int port)
{
	struct ib_event_work *ew;

	ew = kmalloc(sizeof(*ew), GFP_ATOMIC);
	if (ew) {
		INIT_WORK(&ew->work, ib_sl2vl_update_work);
		ew->port = port;
		ew->ib_dev = ibdev;
		queue_work(wq, &ew->work);
	}
}

static void mlx4_ib_event(struct mlx4_dev *dev, void *ibdev_ptr,
			  enum mlx4_dev_event event, unsigned long param)
{
	struct ib_event ibev;
	struct mlx4_ib_dev *ibdev = to_mdev((struct ib_device *) ibdev_ptr);
	struct mlx4_eqe *eqe = NULL;
	struct ib_event_work *ew;
	int p = 0;

	if (mlx4_is_bonded(dev) &&
	    ((event == MLX4_DEV_EVENT_PORT_UP) ||
	    (event == MLX4_DEV_EVENT_PORT_DOWN))) {
		ew = kmalloc(sizeof(*ew), GFP_ATOMIC);
		if (!ew)
			return;
		INIT_WORK(&ew->work, handle_bonded_port_state_event);
		ew->ib_dev = ibdev;
		queue_work(wq, &ew->work);
		return;
	}

	if (event == MLX4_DEV_EVENT_PORT_MGMT_CHANGE)
		eqe = (struct mlx4_eqe *)param;
	else
		p = (int) param;

	switch (event) {
	case MLX4_DEV_EVENT_PORT_UP:
		if (p > ibdev->num_ports)
			return;
		if (!mlx4_is_slave(dev) &&
		    rdma_port_get_link_layer(&ibdev->ib_dev, p) ==
			IB_LINK_LAYER_INFINIBAND) {
			if (mlx4_is_master(dev))
				mlx4_ib_invalidate_all_guid_record(ibdev, p);
			if (ibdev->dev->flags & MLX4_FLAG_SECURE_HOST &&
			    !(ibdev->dev->caps.flags2 & MLX4_DEV_CAP_FLAG2_SL_TO_VL_CHANGE_EVENT))
				mlx4_sched_ib_sl2vl_update_work(ibdev, p);
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
		mlx4_ib_handle_catas_error(ibdev);
		break;

	case MLX4_DEV_EVENT_PORT_MGMT_CHANGE:
		ew = kmalloc(sizeof *ew, GFP_ATOMIC);
		if (!ew)
			break;

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
		if (mlx4_is_master(dev)) {
			int i;

			for (i = 1; i <= ibdev->num_ports; i++) {
				if (rdma_port_get_link_layer(&ibdev->ib_dev, i)
					== IB_LINK_LAYER_INFINIBAND)
					mlx4_ib_slave_alias_guid_event(ibdev,
								       p, i,
								       1);
			}
		}
		return;

	case MLX4_DEV_EVENT_SLAVE_SHUTDOWN:
		if (mlx4_is_master(dev)) {
			int i;

			for (i = 1; i <= ibdev->num_ports; i++) {
				if (rdma_port_get_link_layer(&ibdev->ib_dev, i)
					== IB_LINK_LAYER_INFINIBAND)
					mlx4_ib_slave_alias_guid_event(ibdev,
								       p, i,
								       0);
			}
		}
		/* here, p is the slave id */
		do_slave_init(ibdev, p, 0);
		return;

	default:
		return;
	}

	ibev.device	      = ibdev_ptr;
	ibev.element.port_num = mlx4_is_bonded(ibdev->dev) ? 1 : (u8)p;

	ib_dispatch_event(&ibev);
}

static struct mlx4_interface mlx4_ib_interface = {
	.add		= mlx4_ib_add,
	.remove		= mlx4_ib_remove,
	.event		= mlx4_ib_event,
	.protocol	= MLX4_PROT_IB_IPV6,
	.flags		= MLX4_INTFF_BONDING
};

static int __init mlx4_ib_init(void)
{
	int err;

	wq = alloc_ordered_workqueue("mlx4_ib", WQ_MEM_RECLAIM);
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
