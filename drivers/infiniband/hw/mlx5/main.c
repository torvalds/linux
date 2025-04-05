// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2013-2020, Mellanox Technologies inc. All rights reserved.
 * Copyright (c) 2020, Intel Corporation. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/delay.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>
#include <linux/mlx5/port.h>
#include <linux/mlx5/vport.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/eswitch.h>
#include <linux/mlx5/driver.h>
#include <linux/list.h>
#include <rdma/ib_smi.h>
#include <rdma/ib_umem_odp.h>
#include <rdma/lag.h>
#include <linux/in.h>
#include <linux/etherdevice.h>
#include "mlx5_ib.h"
#include "ib_rep.h"
#include "cmd.h"
#include "devx.h"
#include "dm.h"
#include "fs.h"
#include "srq.h"
#include "qp.h"
#include "wr.h"
#include "restrack.h"
#include "counters.h"
#include "umr.h"
#include <rdma/uverbs_std_types.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/mlx5_user_ioctl_verbs.h>
#include <rdma/mlx5_user_ioctl_cmds.h>
#include "macsec.h"
#include "data_direct.h"

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

MODULE_AUTHOR("Eli Cohen <eli@mellanox.com>");
MODULE_DESCRIPTION("Mellanox 5th generation network adapters (ConnectX series) IB driver");
MODULE_LICENSE("Dual BSD/GPL");

struct mlx5_ib_event_work {
	struct work_struct	work;
	union {
		struct mlx5_ib_dev	      *dev;
		struct mlx5_ib_multiport_info *mpi;
	};
	bool			is_slave;
	unsigned int		event;
	void			*param;
};

enum {
	MLX5_ATOMIC_SIZE_QP_8BYTES = 1 << 3,
};

static struct workqueue_struct *mlx5_ib_event_wq;
static LIST_HEAD(mlx5_ib_unaffiliated_port_list);
static LIST_HEAD(mlx5_ib_dev_list);
/*
 * This mutex should be held when accessing either of the above lists
 */
static DEFINE_MUTEX(mlx5_ib_multiport_mutex);

struct mlx5_ib_dev *mlx5_ib_get_ibdev_from_mpi(struct mlx5_ib_multiport_info *mpi)
{
	struct mlx5_ib_dev *dev;

	mutex_lock(&mlx5_ib_multiport_mutex);
	dev = mpi->ibdev;
	mutex_unlock(&mlx5_ib_multiport_mutex);
	return dev;
}

static enum rdma_link_layer
mlx5_port_type_cap_to_rdma_ll(int port_type_cap)
{
	switch (port_type_cap) {
	case MLX5_CAP_PORT_TYPE_IB:
		return IB_LINK_LAYER_INFINIBAND;
	case MLX5_CAP_PORT_TYPE_ETH:
		return IB_LINK_LAYER_ETHERNET;
	default:
		return IB_LINK_LAYER_UNSPECIFIED;
	}
}

static enum rdma_link_layer
mlx5_ib_port_link_layer(struct ib_device *device, u32 port_num)
{
	struct mlx5_ib_dev *dev = to_mdev(device);
	int port_type_cap = MLX5_CAP_GEN(dev->mdev, port_type);

	return mlx5_port_type_cap_to_rdma_ll(port_type_cap);
}

static int get_port_state(struct ib_device *ibdev,
			  u32 port_num,
			  enum ib_port_state *state)
{
	struct ib_port_attr attr;
	int ret;

	memset(&attr, 0, sizeof(attr));
	ret = ibdev->ops.query_port(ibdev, port_num, &attr);
	if (!ret)
		*state = attr.state;
	return ret;
}

static struct mlx5_roce *mlx5_get_rep_roce(struct mlx5_ib_dev *dev,
					   struct net_device *ndev,
					   struct net_device *upper,
					   u32 *port_num)
{
	struct net_device *rep_ndev;
	struct mlx5_ib_port *port;
	int i;

	for (i = 0; i < dev->num_ports; i++) {
		port  = &dev->port[i];
		if (!port->rep)
			continue;

		if (upper == ndev && port->rep->vport == MLX5_VPORT_UPLINK) {
			*port_num = i + 1;
			return &port->roce;
		}

		if (upper && port->rep->vport == MLX5_VPORT_UPLINK)
			continue;
		rep_ndev = ib_device_get_netdev(&dev->ib_dev, i + 1);
		if (rep_ndev && rep_ndev == ndev) {
			dev_put(rep_ndev);
			*port_num = i + 1;
			return &port->roce;
		}

		dev_put(rep_ndev);
	}

	return NULL;
}

static bool mlx5_netdev_send_event(struct mlx5_ib_dev *dev,
				   struct net_device *ndev,
				   struct net_device *upper,
				   struct net_device *ib_ndev)
{
	if (!dev->ib_active)
		return false;

	/* Event is about our upper device */
	if (upper == ndev)
		return true;

	/* RDMA device is not in lag and not in switchdev */
	if (!dev->is_rep && !upper && ndev == ib_ndev)
		return true;

	/* RDMA devie is in switchdev */
	if (dev->is_rep && ndev == ib_ndev)
		return true;

	return false;
}

static struct net_device *mlx5_ib_get_rep_uplink_netdev(struct mlx5_ib_dev *ibdev)
{
	struct mlx5_ib_port *port;
	int i;

	for (i = 0; i < ibdev->num_ports; i++) {
		port = &ibdev->port[i];
		if (port->rep && port->rep->vport == MLX5_VPORT_UPLINK) {
			return ib_device_get_netdev(&ibdev->ib_dev, i + 1);
		}
	}

	return NULL;
}

static int mlx5_netdev_event(struct notifier_block *this,
			     unsigned long event, void *ptr)
{
	struct mlx5_roce *roce = container_of(this, struct mlx5_roce, nb);
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	u32 port_num = roce->native_port_num;
	struct net_device *ib_ndev = NULL;
	struct mlx5_core_dev *mdev;
	struct mlx5_ib_dev *ibdev;

	ibdev = roce->dev;
	mdev = mlx5_ib_get_native_port_mdev(ibdev, port_num, NULL);
	if (!mdev)
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_REGISTER:
		/* Should already be registered during the load */
		if (ibdev->is_rep)
			break;

		ib_ndev = ib_device_get_netdev(&ibdev->ib_dev, port_num);
		/* Exit if already registered */
		if (ib_ndev)
			goto put_ndev;

		if (ndev->dev.parent == mdev->device)
			ib_device_set_netdev(&ibdev->ib_dev, ndev, port_num);
		break;

	case NETDEV_UNREGISTER:
		/* In case of reps, ib device goes away before the netdevs */
		if (ibdev->is_rep)
			break;
		ib_ndev = ib_device_get_netdev(&ibdev->ib_dev, port_num);
		if (ib_ndev == ndev)
			ib_device_set_netdev(&ibdev->ib_dev, NULL, port_num);
		goto put_ndev;

	case NETDEV_CHANGE:
	case NETDEV_UP:
	case NETDEV_DOWN: {
		struct net_device *upper = NULL;

		if (!netif_is_lag_master(ndev) && !netif_is_lag_port(ndev) &&
		    !mlx5_core_mp_enabled(mdev))
			return NOTIFY_DONE;

		if (mlx5_lag_is_roce(mdev) || mlx5_lag_is_sriov(mdev)) {
			struct net_device *lag_ndev;

			if(mlx5_lag_is_roce(mdev))
				lag_ndev = ib_device_get_netdev(&ibdev->ib_dev, 1);
			else /* sriov lag */
				lag_ndev = mlx5_ib_get_rep_uplink_netdev(ibdev);

			if (lag_ndev) {
				upper = netdev_master_upper_dev_get(lag_ndev);
				dev_put(lag_ndev);
			} else {
				goto done;
			}
		}

		if (ibdev->is_rep)
			roce = mlx5_get_rep_roce(ibdev, ndev, upper, &port_num);
		if (!roce)
			return NOTIFY_DONE;

		ib_ndev = ib_device_get_netdev(&ibdev->ib_dev, port_num);

		if (mlx5_netdev_send_event(ibdev, ndev, upper, ib_ndev)) {
			struct ib_event ibev = { };
			enum ib_port_state port_state;

			if (get_port_state(&ibdev->ib_dev, port_num,
					   &port_state))
				goto put_ndev;

			if (roce->last_port_state == port_state)
				goto put_ndev;

			roce->last_port_state = port_state;
			ibev.device = &ibdev->ib_dev;
			if (port_state == IB_PORT_DOWN)
				ibev.event = IB_EVENT_PORT_ERR;
			else if (port_state == IB_PORT_ACTIVE)
				ibev.event = IB_EVENT_PORT_ACTIVE;
			else
				goto put_ndev;

			ibev.element.port_num = port_num;
			ib_dispatch_event(&ibev);
		}
		break;
	}

	default:
		break;
	}
put_ndev:
	dev_put(ib_ndev);
done:
	mlx5_ib_put_native_port_mdev(ibdev, port_num);
	return NOTIFY_DONE;
}

struct mlx5_core_dev *mlx5_ib_get_native_port_mdev(struct mlx5_ib_dev *ibdev,
						   u32 ib_port_num,
						   u32 *native_port_num)
{
	enum rdma_link_layer ll = mlx5_ib_port_link_layer(&ibdev->ib_dev,
							  ib_port_num);
	struct mlx5_core_dev *mdev = NULL;
	struct mlx5_ib_multiport_info *mpi;
	struct mlx5_ib_port *port;

	if (ibdev->ib_dev.type == RDMA_DEVICE_TYPE_SMI) {
		if (native_port_num)
			*native_port_num = smi_to_native_portnum(ibdev,
								 ib_port_num);
		return ibdev->mdev;

	}

	if (!mlx5_core_mp_enabled(ibdev->mdev) ||
	    ll != IB_LINK_LAYER_ETHERNET) {
		if (native_port_num)
			*native_port_num = ib_port_num;
		return ibdev->mdev;
	}

	if (native_port_num)
		*native_port_num = 1;

	port = &ibdev->port[ib_port_num - 1];
	spin_lock(&port->mp.mpi_lock);
	mpi = ibdev->port[ib_port_num - 1].mp.mpi;
	if (mpi && !mpi->unaffiliate) {
		mdev = mpi->mdev;
		/* If it's the master no need to refcount, it'll exist
		 * as long as the ib_dev exists.
		 */
		if (!mpi->is_master)
			mpi->mdev_refcnt++;
	}
	spin_unlock(&port->mp.mpi_lock);

	return mdev;
}

void mlx5_ib_put_native_port_mdev(struct mlx5_ib_dev *ibdev, u32 port_num)
{
	enum rdma_link_layer ll = mlx5_ib_port_link_layer(&ibdev->ib_dev,
							  port_num);
	struct mlx5_ib_multiport_info *mpi;
	struct mlx5_ib_port *port;

	if (!mlx5_core_mp_enabled(ibdev->mdev) || ll != IB_LINK_LAYER_ETHERNET)
		return;

	port = &ibdev->port[port_num - 1];

	spin_lock(&port->mp.mpi_lock);
	mpi = ibdev->port[port_num - 1].mp.mpi;
	if (mpi->is_master)
		goto out;

	mpi->mdev_refcnt--;
	if (mpi->unaffiliate)
		complete(&mpi->unref_comp);
out:
	spin_unlock(&port->mp.mpi_lock);
}

static int translate_eth_legacy_proto_oper(u32 eth_proto_oper,
					   u16 *active_speed, u8 *active_width)
{
	switch (eth_proto_oper) {
	case MLX5E_PROT_MASK(MLX5E_1000BASE_CX_SGMII):
	case MLX5E_PROT_MASK(MLX5E_1000BASE_KX):
	case MLX5E_PROT_MASK(MLX5E_100BASE_TX):
	case MLX5E_PROT_MASK(MLX5E_1000BASE_T):
		*active_width = IB_WIDTH_1X;
		*active_speed = IB_SPEED_SDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_10GBASE_T):
	case MLX5E_PROT_MASK(MLX5E_10GBASE_CX4):
	case MLX5E_PROT_MASK(MLX5E_10GBASE_KX4):
	case MLX5E_PROT_MASK(MLX5E_10GBASE_KR):
	case MLX5E_PROT_MASK(MLX5E_10GBASE_CR):
	case MLX5E_PROT_MASK(MLX5E_10GBASE_SR):
	case MLX5E_PROT_MASK(MLX5E_10GBASE_ER):
		*active_width = IB_WIDTH_1X;
		*active_speed = IB_SPEED_QDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_25GBASE_CR):
	case MLX5E_PROT_MASK(MLX5E_25GBASE_KR):
	case MLX5E_PROT_MASK(MLX5E_25GBASE_SR):
		*active_width = IB_WIDTH_1X;
		*active_speed = IB_SPEED_EDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_40GBASE_CR4):
	case MLX5E_PROT_MASK(MLX5E_40GBASE_KR4):
	case MLX5E_PROT_MASK(MLX5E_40GBASE_SR4):
	case MLX5E_PROT_MASK(MLX5E_40GBASE_LR4):
		*active_width = IB_WIDTH_4X;
		*active_speed = IB_SPEED_QDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_50GBASE_CR2):
	case MLX5E_PROT_MASK(MLX5E_50GBASE_KR2):
	case MLX5E_PROT_MASK(MLX5E_50GBASE_SR2):
		*active_width = IB_WIDTH_1X;
		*active_speed = IB_SPEED_HDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_56GBASE_R4):
		*active_width = IB_WIDTH_4X;
		*active_speed = IB_SPEED_FDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_100GBASE_CR4):
	case MLX5E_PROT_MASK(MLX5E_100GBASE_SR4):
	case MLX5E_PROT_MASK(MLX5E_100GBASE_KR4):
	case MLX5E_PROT_MASK(MLX5E_100GBASE_LR4):
		*active_width = IB_WIDTH_4X;
		*active_speed = IB_SPEED_EDR;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int translate_eth_ext_proto_oper(u32 eth_proto_oper, u16 *active_speed,
					u8 *active_width)
{
	switch (eth_proto_oper) {
	case MLX5E_PROT_MASK(MLX5E_SGMII_100M):
	case MLX5E_PROT_MASK(MLX5E_1000BASE_X_SGMII):
		*active_width = IB_WIDTH_1X;
		*active_speed = IB_SPEED_SDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_5GBASE_R):
		*active_width = IB_WIDTH_1X;
		*active_speed = IB_SPEED_DDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_10GBASE_XFI_XAUI_1):
		*active_width = IB_WIDTH_1X;
		*active_speed = IB_SPEED_QDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_40GBASE_XLAUI_4_XLPPI_4):
		*active_width = IB_WIDTH_4X;
		*active_speed = IB_SPEED_QDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_25GAUI_1_25GBASE_CR_KR):
		*active_width = IB_WIDTH_1X;
		*active_speed = IB_SPEED_EDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_50GAUI_2_LAUI_2_50GBASE_CR2_KR2):
		*active_width = IB_WIDTH_2X;
		*active_speed = IB_SPEED_EDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_50GAUI_1_LAUI_1_50GBASE_CR_KR):
		*active_width = IB_WIDTH_1X;
		*active_speed = IB_SPEED_HDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_CAUI_4_100GBASE_CR4_KR4):
		*active_width = IB_WIDTH_4X;
		*active_speed = IB_SPEED_EDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_100GAUI_2_100GBASE_CR2_KR2):
		*active_width = IB_WIDTH_2X;
		*active_speed = IB_SPEED_HDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_100GAUI_1_100GBASE_CR_KR):
		*active_width = IB_WIDTH_1X;
		*active_speed = IB_SPEED_NDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_200GAUI_4_200GBASE_CR4_KR4):
		*active_width = IB_WIDTH_4X;
		*active_speed = IB_SPEED_HDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_200GAUI_2_200GBASE_CR2_KR2):
		*active_width = IB_WIDTH_2X;
		*active_speed = IB_SPEED_NDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_400GAUI_8_400GBASE_CR8):
		*active_width = IB_WIDTH_8X;
		*active_speed = IB_SPEED_HDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_400GAUI_4_400GBASE_CR4_KR4):
		*active_width = IB_WIDTH_4X;
		*active_speed = IB_SPEED_NDR;
		break;
	case MLX5E_PROT_MASK(MLX5E_800GAUI_8_800GBASE_CR8_KR8):
		*active_width = IB_WIDTH_8X;
		*active_speed = IB_SPEED_NDR;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int translate_eth_proto_oper(u32 eth_proto_oper, u16 *active_speed,
				    u8 *active_width, bool ext)
{
	return ext ?
		translate_eth_ext_proto_oper(eth_proto_oper, active_speed,
					     active_width) :
		translate_eth_legacy_proto_oper(eth_proto_oper, active_speed,
						active_width);
}

static int mlx5_query_port_roce(struct ib_device *device, u32 port_num,
				struct ib_port_attr *props)
{
	struct mlx5_ib_dev *dev = to_mdev(device);
	u32 out[MLX5_ST_SZ_DW(ptys_reg)] = {0};
	struct mlx5_core_dev *mdev;
	struct net_device *ndev, *upper;
	enum ib_mtu ndev_ib_mtu;
	bool put_mdev = true;
	u32 eth_prot_oper;
	u32 mdev_port_num;
	bool ext;
	int err;

	mdev = mlx5_ib_get_native_port_mdev(dev, port_num, &mdev_port_num);
	if (!mdev) {
		/* This means the port isn't affiliated yet. Get the
		 * info for the master port instead.
		 */
		put_mdev = false;
		mdev = dev->mdev;
		mdev_port_num = 1;
		port_num = 1;
	}

	/* Possible bad flows are checked before filling out props so in case
	 * of an error it will still be zeroed out.
	 * Use native port in case of reps
	 */
	if (dev->is_rep)
		err = mlx5_query_port_ptys(mdev, out, sizeof(out), MLX5_PTYS_EN,
					   1, 0);
	else
		err = mlx5_query_port_ptys(mdev, out, sizeof(out), MLX5_PTYS_EN,
					   mdev_port_num, 0);
	if (err)
		goto out;
	ext = !!MLX5_GET_ETH_PROTO(ptys_reg, out, true, eth_proto_capability);
	eth_prot_oper = MLX5_GET_ETH_PROTO(ptys_reg, out, ext, eth_proto_oper);

	props->active_width     = IB_WIDTH_4X;
	props->active_speed     = IB_SPEED_QDR;

	translate_eth_proto_oper(eth_prot_oper, &props->active_speed,
				 &props->active_width, ext);

	if (!dev->is_rep && dev->mdev->roce.roce_en) {
		u16 qkey_viol_cntr;

		props->port_cap_flags |= IB_PORT_CM_SUP;
		props->ip_gids = true;
		props->gid_tbl_len = MLX5_CAP_ROCE(dev->mdev,
						   roce_address_table_size);
		mlx5_query_nic_vport_qkey_viol_cntr(mdev, &qkey_viol_cntr);
		props->qkey_viol_cntr = qkey_viol_cntr;
	}
	props->max_mtu          = IB_MTU_4096;
	props->max_msg_sz       = 1 << MLX5_CAP_GEN(dev->mdev, log_max_msg);
	props->pkey_tbl_len     = 1;
	props->state            = IB_PORT_DOWN;
	props->phys_state       = IB_PORT_PHYS_STATE_DISABLED;

	/* If this is a stub query for an unaffiliated port stop here */
	if (!put_mdev)
		goto out;

	ndev = ib_device_get_netdev(device, port_num);
	if (!ndev)
		goto out;

	if (mlx5_lag_is_roce(mdev) || mlx5_lag_is_sriov(mdev)) {
		rcu_read_lock();
		upper = netdev_master_upper_dev_get_rcu(ndev);
		if (upper) {
			dev_put(ndev);
			ndev = upper;
			dev_hold(ndev);
		}
		rcu_read_unlock();
	}

	if (netif_running(ndev) && netif_carrier_ok(ndev)) {
		props->state      = IB_PORT_ACTIVE;
		props->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	}

	ndev_ib_mtu = iboe_get_mtu(ndev->mtu);

	dev_put(ndev);

	props->active_mtu	= min(props->max_mtu, ndev_ib_mtu);
out:
	if (put_mdev)
		mlx5_ib_put_native_port_mdev(dev, port_num);
	return err;
}

int set_roce_addr(struct mlx5_ib_dev *dev, u32 port_num,
		  unsigned int index, const union ib_gid *gid,
		  const struct ib_gid_attr *attr)
{
	enum ib_gid_type gid_type;
	u16 vlan_id = 0xffff;
	u8 roce_version = 0;
	u8 roce_l3_type = 0;
	u8 mac[ETH_ALEN];
	int ret;

	gid_type = attr->gid_type;
	if (gid) {
		ret = rdma_read_gid_l2_fields(attr, &vlan_id, &mac[0]);
		if (ret)
			return ret;
	}

	switch (gid_type) {
	case IB_GID_TYPE_ROCE:
		roce_version = MLX5_ROCE_VERSION_1;
		break;
	case IB_GID_TYPE_ROCE_UDP_ENCAP:
		roce_version = MLX5_ROCE_VERSION_2;
		if (gid && ipv6_addr_v4mapped((void *)gid))
			roce_l3_type = MLX5_ROCE_L3_TYPE_IPV4;
		else
			roce_l3_type = MLX5_ROCE_L3_TYPE_IPV6;
		break;

	default:
		mlx5_ib_warn(dev, "Unexpected GID type %u\n", gid_type);
	}

	return mlx5_core_roce_gid_set(dev->mdev, index, roce_version,
				      roce_l3_type, gid->raw, mac,
				      vlan_id < VLAN_CFI_MASK, vlan_id,
				      port_num);
}

static int mlx5_ib_add_gid(const struct ib_gid_attr *attr,
			   __always_unused void **context)
{
	int ret;

	ret = mlx5r_add_gid_macsec_operations(attr);
	if (ret)
		return ret;

	return set_roce_addr(to_mdev(attr->device), attr->port_num,
			     attr->index, &attr->gid, attr);
}

static int mlx5_ib_del_gid(const struct ib_gid_attr *attr,
			   __always_unused void **context)
{
	int ret;

	ret = set_roce_addr(to_mdev(attr->device), attr->port_num,
			    attr->index, NULL, attr);
	if (ret)
		return ret;

	mlx5r_del_gid_macsec_operations(attr);
	return 0;
}

__be16 mlx5_get_roce_udp_sport_min(const struct mlx5_ib_dev *dev,
				   const struct ib_gid_attr *attr)
{
	if (attr->gid_type != IB_GID_TYPE_ROCE_UDP_ENCAP)
		return 0;

	return cpu_to_be16(MLX5_CAP_ROCE(dev->mdev, r_roce_min_src_udp_port));
}

static int mlx5_use_mad_ifc(struct mlx5_ib_dev *dev)
{
	if (MLX5_CAP_GEN(dev->mdev, port_type) == MLX5_CAP_PORT_TYPE_IB)
		return !MLX5_CAP_GEN(dev->mdev, ib_virt);
	return 0;
}

enum {
	MLX5_VPORT_ACCESS_METHOD_MAD,
	MLX5_VPORT_ACCESS_METHOD_HCA,
	MLX5_VPORT_ACCESS_METHOD_NIC,
};

static int mlx5_get_vport_access_method(struct ib_device *ibdev)
{
	if (mlx5_use_mad_ifc(to_mdev(ibdev)))
		return MLX5_VPORT_ACCESS_METHOD_MAD;

	if (mlx5_ib_port_link_layer(ibdev, 1) ==
	    IB_LINK_LAYER_ETHERNET)
		return MLX5_VPORT_ACCESS_METHOD_NIC;

	return MLX5_VPORT_ACCESS_METHOD_HCA;
}

static void get_atomic_caps(struct mlx5_ib_dev *dev,
			    u8 atomic_size_qp,
			    struct ib_device_attr *props)
{
	u8 tmp;
	u8 atomic_operations = MLX5_CAP_ATOMIC(dev->mdev, atomic_operations);
	u8 atomic_req_8B_endianness_mode =
		MLX5_CAP_ATOMIC(dev->mdev, atomic_req_8B_endianness_mode);

	/* Check if HW supports 8 bytes standard atomic operations and capable
	 * of host endianness respond
	 */
	tmp = MLX5_ATOMIC_OPS_CMP_SWAP | MLX5_ATOMIC_OPS_FETCH_ADD;
	if (((atomic_operations & tmp) == tmp) &&
	    (atomic_size_qp & MLX5_ATOMIC_SIZE_QP_8BYTES) &&
	    (atomic_req_8B_endianness_mode)) {
		props->atomic_cap = IB_ATOMIC_HCA;
	} else {
		props->atomic_cap = IB_ATOMIC_NONE;
	}
}

static void get_atomic_caps_qp(struct mlx5_ib_dev *dev,
			       struct ib_device_attr *props)
{
	u8 atomic_size_qp = MLX5_CAP_ATOMIC(dev->mdev, atomic_size_qp);

	get_atomic_caps(dev, atomic_size_qp, props);
}

static int mlx5_query_system_image_guid(struct ib_device *ibdev,
					__be64 *sys_image_guid)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_core_dev *mdev = dev->mdev;
	u64 tmp;
	int err;

	switch (mlx5_get_vport_access_method(ibdev)) {
	case MLX5_VPORT_ACCESS_METHOD_MAD:
		return mlx5_query_mad_ifc_system_image_guid(ibdev,
							    sys_image_guid);

	case MLX5_VPORT_ACCESS_METHOD_HCA:
		err = mlx5_query_hca_vport_system_image_guid(mdev, &tmp);
		break;

	case MLX5_VPORT_ACCESS_METHOD_NIC:
		err = mlx5_query_nic_vport_system_image_guid(mdev, &tmp);
		break;

	default:
		return -EINVAL;
	}

	if (!err)
		*sys_image_guid = cpu_to_be64(tmp);

	return err;

}

static int mlx5_query_max_pkeys(struct ib_device *ibdev,
				u16 *max_pkeys)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_core_dev *mdev = dev->mdev;

	switch (mlx5_get_vport_access_method(ibdev)) {
	case MLX5_VPORT_ACCESS_METHOD_MAD:
		return mlx5_query_mad_ifc_max_pkeys(ibdev, max_pkeys);

	case MLX5_VPORT_ACCESS_METHOD_HCA:
	case MLX5_VPORT_ACCESS_METHOD_NIC:
		*max_pkeys = mlx5_to_sw_pkey_sz(MLX5_CAP_GEN(mdev,
						pkey_table_size));
		return 0;

	default:
		return -EINVAL;
	}
}

static int mlx5_query_vendor_id(struct ib_device *ibdev,
				u32 *vendor_id)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);

	switch (mlx5_get_vport_access_method(ibdev)) {
	case MLX5_VPORT_ACCESS_METHOD_MAD:
		return mlx5_query_mad_ifc_vendor_id(ibdev, vendor_id);

	case MLX5_VPORT_ACCESS_METHOD_HCA:
	case MLX5_VPORT_ACCESS_METHOD_NIC:
		return mlx5_core_query_vendor_id(dev->mdev, vendor_id);

	default:
		return -EINVAL;
	}
}

static int mlx5_query_node_guid(struct mlx5_ib_dev *dev,
				__be64 *node_guid)
{
	u64 tmp;
	int err;

	switch (mlx5_get_vport_access_method(&dev->ib_dev)) {
	case MLX5_VPORT_ACCESS_METHOD_MAD:
		return mlx5_query_mad_ifc_node_guid(dev, node_guid);

	case MLX5_VPORT_ACCESS_METHOD_HCA:
		err = mlx5_query_hca_vport_node_guid(dev->mdev, &tmp);
		break;

	case MLX5_VPORT_ACCESS_METHOD_NIC:
		err = mlx5_query_nic_vport_node_guid(dev->mdev, &tmp);
		break;

	default:
		return -EINVAL;
	}

	if (!err)
		*node_guid = cpu_to_be64(tmp);

	return err;
}

struct mlx5_reg_node_desc {
	u8	desc[IB_DEVICE_NODE_DESC_MAX];
};

static int mlx5_query_node_desc(struct mlx5_ib_dev *dev, char *node_desc)
{
	struct mlx5_reg_node_desc in;

	if (mlx5_use_mad_ifc(dev))
		return mlx5_query_mad_ifc_node_desc(dev, node_desc);

	memset(&in, 0, sizeof(in));

	return mlx5_core_access_reg(dev->mdev, &in, sizeof(in), node_desc,
				    sizeof(struct mlx5_reg_node_desc),
				    MLX5_REG_NODE_DESC, 0, 0);
}

static void fill_esw_mgr_reg_c0(struct mlx5_core_dev *mdev,
				struct mlx5_ib_query_device_resp *resp)
{
	struct mlx5_eswitch *esw = mdev->priv.eswitch;
	u16 vport = mlx5_eswitch_manager_vport(mdev);

	resp->reg_c0.value = mlx5_eswitch_get_vport_metadata_for_match(esw,
								      vport);
	resp->reg_c0.mask = mlx5_eswitch_get_vport_metadata_mask();
}

static int mlx5_ib_query_device(struct ib_device *ibdev,
				struct ib_device_attr *props,
				struct ib_udata *uhw)
{
	size_t uhw_outlen = (uhw) ? uhw->outlen : 0;
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_core_dev *mdev = dev->mdev;
	int err = -ENOMEM;
	int max_sq_desc;
	int max_rq_sg;
	int max_sq_sg;
	u64 min_page_size = 1ull << MLX5_CAP_GEN(mdev, log_pg_sz);
	bool raw_support = !mlx5_core_mp_enabled(mdev);
	struct mlx5_ib_query_device_resp resp = {};
	size_t resp_len;
	u64 max_tso;

	resp_len = sizeof(resp.comp_mask) + sizeof(resp.response_length);
	if (uhw_outlen && uhw_outlen < resp_len)
		return -EINVAL;

	resp.response_length = resp_len;

	if (uhw && uhw->inlen && !ib_is_udata_cleared(uhw, 0, uhw->inlen))
		return -EINVAL;

	memset(props, 0, sizeof(*props));
	err = mlx5_query_system_image_guid(ibdev,
					   &props->sys_image_guid);
	if (err)
		return err;

	props->max_pkeys = dev->pkey_table_len;

	err = mlx5_query_vendor_id(ibdev, &props->vendor_id);
	if (err)
		return err;

	props->fw_ver = ((u64)fw_rev_maj(dev->mdev) << 32) |
		(fw_rev_min(dev->mdev) << 16) |
		fw_rev_sub(dev->mdev);
	props->device_cap_flags    = IB_DEVICE_CHANGE_PHY_PORT |
		IB_DEVICE_PORT_ACTIVE_EVENT		|
		IB_DEVICE_SYS_IMAGE_GUID		|
		IB_DEVICE_RC_RNR_NAK_GEN;

	if (MLX5_CAP_GEN(mdev, pkv))
		props->device_cap_flags |= IB_DEVICE_BAD_PKEY_CNTR;
	if (MLX5_CAP_GEN(mdev, qkv))
		props->device_cap_flags |= IB_DEVICE_BAD_QKEY_CNTR;
	if (MLX5_CAP_GEN(mdev, apm))
		props->device_cap_flags |= IB_DEVICE_AUTO_PATH_MIG;
	if (MLX5_CAP_GEN(mdev, xrc))
		props->device_cap_flags |= IB_DEVICE_XRC;
	if (MLX5_CAP_GEN(mdev, imaicl)) {
		props->device_cap_flags |= IB_DEVICE_MEM_WINDOW |
					   IB_DEVICE_MEM_WINDOW_TYPE_2B;
		props->max_mw = 1 << MLX5_CAP_GEN(mdev, log_max_mkey);
		/* We support 'Gappy' memory registration too */
		props->kernel_cap_flags |= IBK_SG_GAPS_REG;
	}
	/* IB_WR_REG_MR always requires changing the entity size with UMR */
	if (!MLX5_CAP_GEN(dev->mdev, umr_modify_entity_size_disabled))
		props->device_cap_flags |= IB_DEVICE_MEM_MGT_EXTENSIONS;
	if (MLX5_CAP_GEN(mdev, sho)) {
		props->kernel_cap_flags |= IBK_INTEGRITY_HANDOVER;
		/* At this stage no support for signature handover */
		props->sig_prot_cap = IB_PROT_T10DIF_TYPE_1 |
				      IB_PROT_T10DIF_TYPE_2 |
				      IB_PROT_T10DIF_TYPE_3;
		props->sig_guard_cap = IB_GUARD_T10DIF_CRC |
				       IB_GUARD_T10DIF_CSUM;
	}
	if (MLX5_CAP_GEN(mdev, block_lb_mc))
		props->kernel_cap_flags |= IBK_BLOCK_MULTICAST_LOOPBACK;

	if (MLX5_CAP_GEN(dev->mdev, eth_net_offloads) && raw_support) {
		if (MLX5_CAP_ETH(mdev, csum_cap)) {
			/* Legacy bit to support old userspace libraries */
			props->device_cap_flags |= IB_DEVICE_RAW_IP_CSUM;
			props->raw_packet_caps |= IB_RAW_PACKET_CAP_IP_CSUM;
		}

		if (MLX5_CAP_ETH(dev->mdev, vlan_cap))
			props->raw_packet_caps |=
				IB_RAW_PACKET_CAP_CVLAN_STRIPPING;

		if (offsetofend(typeof(resp), tso_caps) <= uhw_outlen) {
			max_tso = MLX5_CAP_ETH(mdev, max_lso_cap);
			if (max_tso) {
				resp.tso_caps.max_tso = 1 << max_tso;
				resp.tso_caps.supported_qpts |=
					1 << IB_QPT_RAW_PACKET;
				resp.response_length += sizeof(resp.tso_caps);
			}
		}

		if (offsetofend(typeof(resp), rss_caps) <= uhw_outlen) {
			resp.rss_caps.rx_hash_function =
						MLX5_RX_HASH_FUNC_TOEPLITZ;
			resp.rss_caps.rx_hash_fields_mask =
						MLX5_RX_HASH_SRC_IPV4 |
						MLX5_RX_HASH_DST_IPV4 |
						MLX5_RX_HASH_SRC_IPV6 |
						MLX5_RX_HASH_DST_IPV6 |
						MLX5_RX_HASH_SRC_PORT_TCP |
						MLX5_RX_HASH_DST_PORT_TCP |
						MLX5_RX_HASH_SRC_PORT_UDP |
						MLX5_RX_HASH_DST_PORT_UDP |
						MLX5_RX_HASH_INNER;
			resp.response_length += sizeof(resp.rss_caps);
		}
	} else {
		if (offsetofend(typeof(resp), tso_caps) <= uhw_outlen)
			resp.response_length += sizeof(resp.tso_caps);
		if (offsetofend(typeof(resp), rss_caps) <= uhw_outlen)
			resp.response_length += sizeof(resp.rss_caps);
	}

	if (MLX5_CAP_GEN(mdev, ipoib_basic_offloads)) {
		props->device_cap_flags |= IB_DEVICE_UD_IP_CSUM;
		props->kernel_cap_flags |= IBK_UD_TSO;
	}

	if (MLX5_CAP_GEN(dev->mdev, rq_delay_drop) &&
	    MLX5_CAP_GEN(dev->mdev, general_notification_event) &&
	    raw_support)
		props->raw_packet_caps |= IB_RAW_PACKET_CAP_DELAY_DROP;

	if (MLX5_CAP_GEN(mdev, ipoib_enhanced_offloads) &&
	    MLX5_CAP_IPOIB_ENHANCED(mdev, csum_cap))
		props->device_cap_flags |= IB_DEVICE_UD_IP_CSUM;

	if (MLX5_CAP_GEN(dev->mdev, eth_net_offloads) &&
	    MLX5_CAP_ETH(dev->mdev, scatter_fcs) &&
	    raw_support) {
		/* Legacy bit to support old userspace libraries */
		props->device_cap_flags |= IB_DEVICE_RAW_SCATTER_FCS;
		props->raw_packet_caps |= IB_RAW_PACKET_CAP_SCATTER_FCS;
	}

	if (MLX5_CAP_DEV_MEM(mdev, memic)) {
		props->max_dm_size =
			MLX5_CAP_DEV_MEM(mdev, max_memic_size);
	}

	if (mlx5_get_flow_namespace(dev->mdev, MLX5_FLOW_NAMESPACE_BYPASS))
		props->device_cap_flags |= IB_DEVICE_MANAGED_FLOW_STEERING;

	if (MLX5_CAP_GEN(mdev, end_pad))
		props->device_cap_flags |= IB_DEVICE_PCI_WRITE_END_PADDING;

	props->vendor_part_id	   = mdev->pdev->device;
	props->hw_ver		   = mdev->pdev->revision;

	props->max_mr_size	   = ~0ull;
	props->page_size_cap	   = ~(min_page_size - 1);
	props->max_qp		   = 1 << MLX5_CAP_GEN(mdev, log_max_qp);
	props->max_qp_wr	   = 1 << MLX5_CAP_GEN(mdev, log_max_qp_sz);
	max_rq_sg =  MLX5_CAP_GEN(mdev, max_wqe_sz_rq) /
		     sizeof(struct mlx5_wqe_data_seg);
	max_sq_desc = min_t(int, MLX5_CAP_GEN(mdev, max_wqe_sz_sq), 512);
	max_sq_sg = (max_sq_desc - sizeof(struct mlx5_wqe_ctrl_seg) -
		     sizeof(struct mlx5_wqe_raddr_seg)) /
		sizeof(struct mlx5_wqe_data_seg);
	props->max_send_sge = max_sq_sg;
	props->max_recv_sge = max_rq_sg;
	props->max_sge_rd	   = MLX5_MAX_SGE_RD;
	props->max_cq		   = 1 << MLX5_CAP_GEN(mdev, log_max_cq);
	props->max_cqe = (1 << MLX5_CAP_GEN(mdev, log_max_cq_sz)) - 1;
	props->max_mr		   = 1 << MLX5_CAP_GEN(mdev, log_max_mkey);
	props->max_pd		   = 1 << MLX5_CAP_GEN(mdev, log_max_pd);
	props->max_qp_rd_atom	   = 1 << MLX5_CAP_GEN(mdev, log_max_ra_req_qp);
	props->max_qp_init_rd_atom = 1 << MLX5_CAP_GEN(mdev, log_max_ra_res_qp);
	props->max_srq		   = 1 << MLX5_CAP_GEN(mdev, log_max_srq);
	props->max_srq_wr = (1 << MLX5_CAP_GEN(mdev, log_max_srq_sz)) - 1;
	props->local_ca_ack_delay  = MLX5_CAP_GEN(mdev, local_ca_ack_delay);
	props->max_res_rd_atom	   = props->max_qp_rd_atom * props->max_qp;
	props->max_srq_sge	   = max_rq_sg - 1;
	props->max_fast_reg_page_list_len =
		1 << MLX5_CAP_GEN(mdev, log_max_klm_list_size);
	props->max_pi_fast_reg_page_list_len =
		props->max_fast_reg_page_list_len / 2;
	props->max_sgl_rd =
		MLX5_CAP_GEN(mdev, max_sgl_for_optimized_performance);
	get_atomic_caps_qp(dev, props);
	props->masked_atomic_cap   = IB_ATOMIC_NONE;
	props->max_mcast_grp	   = 1 << MLX5_CAP_GEN(mdev, log_max_mcg);
	props->max_mcast_qp_attach = MLX5_CAP_GEN(mdev, max_qp_mcg);
	props->max_total_mcast_qp_attach = props->max_mcast_qp_attach *
					   props->max_mcast_grp;
	props->max_ah = INT_MAX;
	props->hca_core_clock = MLX5_CAP_GEN(mdev, device_frequency_khz);
	props->timestamp_mask = 0x7FFFFFFFFFFFFFFFULL;

	if (IS_ENABLED(CONFIG_INFINIBAND_ON_DEMAND_PAGING)) {
		if (dev->odp_caps.general_caps & IB_ODP_SUPPORT)
			props->kernel_cap_flags |= IBK_ON_DEMAND_PAGING;
		props->odp_caps = dev->odp_caps;
		if (!uhw) {
			/* ODP for kernel QPs is not implemented for receive
			 * WQEs and SRQ WQEs
			 */
			props->odp_caps.per_transport_caps.rc_odp_caps &=
				~(IB_ODP_SUPPORT_READ |
				  IB_ODP_SUPPORT_SRQ_RECV);
			props->odp_caps.per_transport_caps.uc_odp_caps &=
				~(IB_ODP_SUPPORT_READ |
				  IB_ODP_SUPPORT_SRQ_RECV);
			props->odp_caps.per_transport_caps.ud_odp_caps &=
				~(IB_ODP_SUPPORT_READ |
				  IB_ODP_SUPPORT_SRQ_RECV);
			props->odp_caps.per_transport_caps.xrc_odp_caps &=
				~(IB_ODP_SUPPORT_READ |
				  IB_ODP_SUPPORT_SRQ_RECV);
		}
	}

	if (mlx5_core_is_vf(mdev))
		props->kernel_cap_flags |= IBK_VIRTUAL_FUNCTION;

	if (mlx5_ib_port_link_layer(ibdev, 1) ==
	    IB_LINK_LAYER_ETHERNET && raw_support) {
		props->rss_caps.max_rwq_indirection_tables =
			1 << MLX5_CAP_GEN(dev->mdev, log_max_rqt);
		props->rss_caps.max_rwq_indirection_table_size =
			1 << MLX5_CAP_GEN(dev->mdev, log_max_rqt_size);
		props->rss_caps.supported_qpts = 1 << IB_QPT_RAW_PACKET;
		props->max_wq_type_rq =
			1 << MLX5_CAP_GEN(dev->mdev, log_max_rq);
	}

	if (MLX5_CAP_GEN(mdev, tag_matching)) {
		props->tm_caps.max_num_tags =
			(1 << MLX5_CAP_GEN(mdev, log_tag_matching_list_sz)) - 1;
		props->tm_caps.max_ops =
			1 << MLX5_CAP_GEN(mdev, log_max_qp_sz);
		props->tm_caps.max_sge = MLX5_TM_MAX_SGE;
	}

	if (MLX5_CAP_GEN(mdev, tag_matching) &&
	    MLX5_CAP_GEN(mdev, rndv_offload_rc)) {
		props->tm_caps.flags = IB_TM_CAP_RNDV_RC;
		props->tm_caps.max_rndv_hdr_size = MLX5_TM_MAX_RNDV_MSG_SIZE;
	}

	if (MLX5_CAP_GEN(dev->mdev, cq_moderation)) {
		props->cq_caps.max_cq_moderation_count =
						MLX5_MAX_CQ_COUNT;
		props->cq_caps.max_cq_moderation_period =
						MLX5_MAX_CQ_PERIOD;
	}

	if (offsetofend(typeof(resp), cqe_comp_caps) <= uhw_outlen) {
		resp.response_length += sizeof(resp.cqe_comp_caps);

		if (MLX5_CAP_GEN(dev->mdev, cqe_compression)) {
			resp.cqe_comp_caps.max_num =
				MLX5_CAP_GEN(dev->mdev,
					     cqe_compression_max_num);

			resp.cqe_comp_caps.supported_format =
				MLX5_IB_CQE_RES_FORMAT_HASH |
				MLX5_IB_CQE_RES_FORMAT_CSUM;

			if (MLX5_CAP_GEN(dev->mdev, mini_cqe_resp_stride_index))
				resp.cqe_comp_caps.supported_format |=
					MLX5_IB_CQE_RES_FORMAT_CSUM_STRIDX;
		}
	}

	if (offsetofend(typeof(resp), packet_pacing_caps) <= uhw_outlen &&
	    raw_support) {
		if (MLX5_CAP_QOS(mdev, packet_pacing) &&
		    MLX5_CAP_GEN(mdev, qos)) {
			resp.packet_pacing_caps.qp_rate_limit_max =
				MLX5_CAP_QOS(mdev, packet_pacing_max_rate);
			resp.packet_pacing_caps.qp_rate_limit_min =
				MLX5_CAP_QOS(mdev, packet_pacing_min_rate);
			resp.packet_pacing_caps.supported_qpts |=
				1 << IB_QPT_RAW_PACKET;
			if (MLX5_CAP_QOS(mdev, packet_pacing_burst_bound) &&
			    MLX5_CAP_QOS(mdev, packet_pacing_typical_size))
				resp.packet_pacing_caps.cap_flags |=
					MLX5_IB_PP_SUPPORT_BURST;
		}
		resp.response_length += sizeof(resp.packet_pacing_caps);
	}

	if (offsetofend(typeof(resp), mlx5_ib_support_multi_pkt_send_wqes) <=
	    uhw_outlen) {
		if (MLX5_CAP_ETH(mdev, multi_pkt_send_wqe))
			resp.mlx5_ib_support_multi_pkt_send_wqes =
				MLX5_IB_ALLOW_MPW;

		if (MLX5_CAP_ETH(mdev, enhanced_multi_pkt_send_wqe))
			resp.mlx5_ib_support_multi_pkt_send_wqes |=
				MLX5_IB_SUPPORT_EMPW;

		resp.response_length +=
			sizeof(resp.mlx5_ib_support_multi_pkt_send_wqes);
	}

	if (offsetofend(typeof(resp), flags) <= uhw_outlen) {
		resp.response_length += sizeof(resp.flags);

		if (MLX5_CAP_GEN(mdev, cqe_compression_128))
			resp.flags |=
				MLX5_IB_QUERY_DEV_RESP_FLAGS_CQE_128B_COMP;

		if (MLX5_CAP_GEN(mdev, cqe_128_always))
			resp.flags |= MLX5_IB_QUERY_DEV_RESP_FLAGS_CQE_128B_PAD;
		if (MLX5_CAP_GEN(mdev, qp_packet_based))
			resp.flags |=
				MLX5_IB_QUERY_DEV_RESP_PACKET_BASED_CREDIT_MODE;

		resp.flags |= MLX5_IB_QUERY_DEV_RESP_FLAGS_SCAT2CQE_DCT;

		if (MLX5_CAP_GEN_2(mdev, dp_ordering_force) &&
		    (MLX5_CAP_GEN(mdev, dp_ordering_ooo_all_xrc) ||
		    MLX5_CAP_GEN(mdev, dp_ordering_ooo_all_dc) ||
		    MLX5_CAP_GEN(mdev, dp_ordering_ooo_all_rc) ||
		    MLX5_CAP_GEN(mdev, dp_ordering_ooo_all_ud) ||
		    MLX5_CAP_GEN(mdev, dp_ordering_ooo_all_uc)))
			resp.flags |= MLX5_IB_QUERY_DEV_RESP_FLAGS_OOO_DP;
	}

	if (offsetofend(typeof(resp), sw_parsing_caps) <= uhw_outlen) {
		resp.response_length += sizeof(resp.sw_parsing_caps);
		if (MLX5_CAP_ETH(mdev, swp)) {
			resp.sw_parsing_caps.sw_parsing_offloads |=
				MLX5_IB_SW_PARSING;

			if (MLX5_CAP_ETH(mdev, swp_csum))
				resp.sw_parsing_caps.sw_parsing_offloads |=
					MLX5_IB_SW_PARSING_CSUM;

			if (MLX5_CAP_ETH(mdev, swp_lso))
				resp.sw_parsing_caps.sw_parsing_offloads |=
					MLX5_IB_SW_PARSING_LSO;

			if (resp.sw_parsing_caps.sw_parsing_offloads)
				resp.sw_parsing_caps.supported_qpts =
					BIT(IB_QPT_RAW_PACKET);
		}
	}

	if (offsetofend(typeof(resp), striding_rq_caps) <= uhw_outlen &&
	    raw_support) {
		resp.response_length += sizeof(resp.striding_rq_caps);
		if (MLX5_CAP_GEN(mdev, striding_rq)) {
			resp.striding_rq_caps.min_single_stride_log_num_of_bytes =
				MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES;
			resp.striding_rq_caps.max_single_stride_log_num_of_bytes =
				MLX5_MAX_SINGLE_STRIDE_LOG_NUM_BYTES;
			if (MLX5_CAP_GEN(dev->mdev, ext_stride_num_range))
				resp.striding_rq_caps
					.min_single_wqe_log_num_of_strides =
					MLX5_EXT_MIN_SINGLE_WQE_LOG_NUM_STRIDES;
			else
				resp.striding_rq_caps
					.min_single_wqe_log_num_of_strides =
					MLX5_MIN_SINGLE_WQE_LOG_NUM_STRIDES;
			resp.striding_rq_caps.max_single_wqe_log_num_of_strides =
				MLX5_MAX_SINGLE_WQE_LOG_NUM_STRIDES;
			resp.striding_rq_caps.supported_qpts =
				BIT(IB_QPT_RAW_PACKET);
		}
	}

	if (offsetofend(typeof(resp), tunnel_offloads_caps) <= uhw_outlen) {
		resp.response_length += sizeof(resp.tunnel_offloads_caps);
		if (MLX5_CAP_ETH(mdev, tunnel_stateless_vxlan))
			resp.tunnel_offloads_caps |=
				MLX5_IB_TUNNELED_OFFLOADS_VXLAN;
		if (MLX5_CAP_ETH(mdev, tunnel_stateless_geneve_rx))
			resp.tunnel_offloads_caps |=
				MLX5_IB_TUNNELED_OFFLOADS_GENEVE;
		if (MLX5_CAP_ETH(mdev, tunnel_stateless_gre))
			resp.tunnel_offloads_caps |=
				MLX5_IB_TUNNELED_OFFLOADS_GRE;
		if (MLX5_CAP_ETH(mdev, tunnel_stateless_mpls_over_gre))
			resp.tunnel_offloads_caps |=
				MLX5_IB_TUNNELED_OFFLOADS_MPLS_GRE;
		if (MLX5_CAP_ETH(mdev, tunnel_stateless_mpls_over_udp))
			resp.tunnel_offloads_caps |=
				MLX5_IB_TUNNELED_OFFLOADS_MPLS_UDP;
	}

	if (offsetofend(typeof(resp), dci_streams_caps) <= uhw_outlen) {
		resp.response_length += sizeof(resp.dci_streams_caps);

		resp.dci_streams_caps.max_log_num_concurent =
			MLX5_CAP_GEN(mdev, log_max_dci_stream_channels);

		resp.dci_streams_caps.max_log_num_errored =
			MLX5_CAP_GEN(mdev, log_max_dci_errored_streams);
	}

	if (offsetofend(typeof(resp), reserved) <= uhw_outlen)
		resp.response_length += sizeof(resp.reserved);

	if (offsetofend(typeof(resp), reg_c0) <= uhw_outlen) {
		struct mlx5_eswitch *esw = mdev->priv.eswitch;

		resp.response_length += sizeof(resp.reg_c0);

		if (mlx5_eswitch_mode(mdev) == MLX5_ESWITCH_OFFLOADS &&
		    mlx5_eswitch_vport_match_metadata_enabled(esw))
			fill_esw_mgr_reg_c0(mdev, &resp);
	}

	if (uhw_outlen) {
		err = ib_copy_to_udata(uhw, &resp, resp.response_length);

		if (err)
			return err;
	}

	return 0;
}

static void translate_active_width(struct ib_device *ibdev, u16 active_width,
				   u8 *ib_width)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);

	if (active_width & MLX5_PTYS_WIDTH_1X)
		*ib_width = IB_WIDTH_1X;
	else if (active_width & MLX5_PTYS_WIDTH_2X)
		*ib_width = IB_WIDTH_2X;
	else if (active_width & MLX5_PTYS_WIDTH_4X)
		*ib_width = IB_WIDTH_4X;
	else if (active_width & MLX5_PTYS_WIDTH_8X)
		*ib_width = IB_WIDTH_8X;
	else if (active_width & MLX5_PTYS_WIDTH_12X)
		*ib_width = IB_WIDTH_12X;
	else {
		mlx5_ib_dbg(dev, "Invalid active_width %d, setting width to default value: 4x\n",
			    active_width);
		*ib_width = IB_WIDTH_4X;
	}

	return;
}

static int mlx5_mtu_to_ib_mtu(int mtu)
{
	switch (mtu) {
	case 256: return 1;
	case 512: return 2;
	case 1024: return 3;
	case 2048: return 4;
	case 4096: return 5;
	default:
		pr_warn("invalid mtu\n");
		return -1;
	}
}

enum ib_max_vl_num {
	__IB_MAX_VL_0		= 1,
	__IB_MAX_VL_0_1		= 2,
	__IB_MAX_VL_0_3		= 3,
	__IB_MAX_VL_0_7		= 4,
	__IB_MAX_VL_0_14	= 5,
};

enum mlx5_vl_hw_cap {
	MLX5_VL_HW_0	= 1,
	MLX5_VL_HW_0_1	= 2,
	MLX5_VL_HW_0_2	= 3,
	MLX5_VL_HW_0_3	= 4,
	MLX5_VL_HW_0_4	= 5,
	MLX5_VL_HW_0_5	= 6,
	MLX5_VL_HW_0_6	= 7,
	MLX5_VL_HW_0_7	= 8,
	MLX5_VL_HW_0_14	= 15
};

static int translate_max_vl_num(struct ib_device *ibdev, u8 vl_hw_cap,
				u8 *max_vl_num)
{
	switch (vl_hw_cap) {
	case MLX5_VL_HW_0:
		*max_vl_num = __IB_MAX_VL_0;
		break;
	case MLX5_VL_HW_0_1:
		*max_vl_num = __IB_MAX_VL_0_1;
		break;
	case MLX5_VL_HW_0_3:
		*max_vl_num = __IB_MAX_VL_0_3;
		break;
	case MLX5_VL_HW_0_7:
		*max_vl_num = __IB_MAX_VL_0_7;
		break;
	case MLX5_VL_HW_0_14:
		*max_vl_num = __IB_MAX_VL_0_14;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int mlx5_query_hca_port(struct ib_device *ibdev, u32 port,
			       struct ib_port_attr *props)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_hca_vport_context *rep;
	u8 vl_hw_cap, plane_index = 0;
	u16 max_mtu;
	u16 oper_mtu;
	int err;
	u16 ib_link_width_oper;

	rep = kzalloc(sizeof(*rep), GFP_KERNEL);
	if (!rep) {
		err = -ENOMEM;
		goto out;
	}

	/* props being zeroed by the caller, avoid zeroing it here */

	if (ibdev->type == RDMA_DEVICE_TYPE_SMI) {
		plane_index = port;
		port = smi_to_native_portnum(dev, port);
	}

	err = mlx5_query_hca_vport_context(mdev, 0, port, 0, rep);
	if (err)
		goto out;

	props->lid		= rep->lid;
	props->lmc		= rep->lmc;
	props->sm_lid		= rep->sm_lid;
	props->sm_sl		= rep->sm_sl;
	props->state		= rep->vport_state;
	props->phys_state	= rep->port_physical_state;

	props->port_cap_flags = rep->cap_mask1;
	if (dev->num_plane) {
		props->port_cap_flags |= IB_PORT_SM_DISABLED;
		props->port_cap_flags &= ~IB_PORT_SM;
	} else if (ibdev->type == RDMA_DEVICE_TYPE_SMI)
		props->port_cap_flags &= ~IB_PORT_CM_SUP;

	props->gid_tbl_len	= mlx5_get_gid_table_len(MLX5_CAP_GEN(mdev, gid_table_size));
	props->max_msg_sz	= 1 << MLX5_CAP_GEN(mdev, log_max_msg);
	props->pkey_tbl_len	= mlx5_to_sw_pkey_sz(MLX5_CAP_GEN(mdev, pkey_table_size));
	props->bad_pkey_cntr	= rep->pkey_violation_counter;
	props->qkey_viol_cntr	= rep->qkey_violation_counter;
	props->subnet_timeout	= rep->subnet_timeout;
	props->init_type_reply	= rep->init_type_reply;

	if (props->port_cap_flags & IB_PORT_CAP_MASK2_SUP)
		props->port_cap_flags2 = rep->cap_mask2;

	err = mlx5_query_ib_port_oper(mdev, &ib_link_width_oper,
				      &props->active_speed, port, plane_index);
	if (err)
		goto out;

	translate_active_width(ibdev, ib_link_width_oper, &props->active_width);

	mlx5_query_port_max_mtu(mdev, &max_mtu, port);

	props->max_mtu = mlx5_mtu_to_ib_mtu(max_mtu);

	mlx5_query_port_oper_mtu(mdev, &oper_mtu, port);

	props->active_mtu = mlx5_mtu_to_ib_mtu(oper_mtu);

	err = mlx5_query_port_vl_hw_cap(mdev, &vl_hw_cap, port);
	if (err)
		goto out;

	err = translate_max_vl_num(ibdev, vl_hw_cap,
				   &props->max_vl_num);
out:
	kfree(rep);
	return err;
}

int mlx5_ib_query_port(struct ib_device *ibdev, u32 port,
		       struct ib_port_attr *props)
{
	unsigned int count;
	int ret;

	switch (mlx5_get_vport_access_method(ibdev)) {
	case MLX5_VPORT_ACCESS_METHOD_MAD:
		ret = mlx5_query_mad_ifc_port(ibdev, port, props);
		break;

	case MLX5_VPORT_ACCESS_METHOD_HCA:
		ret = mlx5_query_hca_port(ibdev, port, props);
		break;

	case MLX5_VPORT_ACCESS_METHOD_NIC:
		ret = mlx5_query_port_roce(ibdev, port, props);
		break;

	default:
		ret = -EINVAL;
	}

	if (!ret && props) {
		struct mlx5_ib_dev *dev = to_mdev(ibdev);
		struct mlx5_core_dev *mdev;
		bool put_mdev = true;

		mdev = mlx5_ib_get_native_port_mdev(dev, port, NULL);
		if (!mdev) {
			/* If the port isn't affiliated yet query the master.
			 * The master and slave will have the same values.
			 */
			mdev = dev->mdev;
			port = 1;
			put_mdev = false;
		}
		count = mlx5_core_reserved_gids_count(mdev);
		if (put_mdev)
			mlx5_ib_put_native_port_mdev(dev, port);
		props->gid_tbl_len -= count;
	}
	return ret;
}

static int mlx5_ib_rep_query_port(struct ib_device *ibdev, u32 port,
				  struct ib_port_attr *props)
{
	return mlx5_query_port_roce(ibdev, port, props);
}

static int mlx5_ib_rep_query_pkey(struct ib_device *ibdev, u32 port, u16 index,
				  u16 *pkey)
{
	/* Default special Pkey for representor device port as per the
	 * IB specification 1.3 section 10.9.1.2.
	 */
	*pkey = 0xffff;
	return 0;
}

static int mlx5_ib_query_gid(struct ib_device *ibdev, u32 port, int index,
			     union ib_gid *gid)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_core_dev *mdev = dev->mdev;

	switch (mlx5_get_vport_access_method(ibdev)) {
	case MLX5_VPORT_ACCESS_METHOD_MAD:
		return mlx5_query_mad_ifc_gids(ibdev, port, index, gid);

	case MLX5_VPORT_ACCESS_METHOD_HCA:
		return mlx5_query_hca_vport_gid(mdev, 0, port, 0, index, gid);

	default:
		return -EINVAL;
	}

}

static int mlx5_query_hca_nic_pkey(struct ib_device *ibdev, u32 port,
				   u16 index, u16 *pkey)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_core_dev *mdev;
	bool put_mdev = true;
	u32 mdev_port_num;
	int err;

	mdev = mlx5_ib_get_native_port_mdev(dev, port, &mdev_port_num);
	if (!mdev) {
		/* The port isn't affiliated yet, get the PKey from the master
		 * port. For RoCE the PKey tables will be the same.
		 */
		put_mdev = false;
		mdev = dev->mdev;
		mdev_port_num = 1;
	}

	err = mlx5_query_hca_vport_pkey(mdev, 0, mdev_port_num, 0,
					index, pkey);
	if (put_mdev)
		mlx5_ib_put_native_port_mdev(dev, port);

	return err;
}

static int mlx5_ib_query_pkey(struct ib_device *ibdev, u32 port, u16 index,
			      u16 *pkey)
{
	switch (mlx5_get_vport_access_method(ibdev)) {
	case MLX5_VPORT_ACCESS_METHOD_MAD:
		return mlx5_query_mad_ifc_pkey(ibdev, port, index, pkey);

	case MLX5_VPORT_ACCESS_METHOD_HCA:
	case MLX5_VPORT_ACCESS_METHOD_NIC:
		return mlx5_query_hca_nic_pkey(ibdev, port, index, pkey);
	default:
		return -EINVAL;
	}
}

static int mlx5_ib_modify_device(struct ib_device *ibdev, int mask,
				 struct ib_device_modify *props)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_reg_node_desc in;
	struct mlx5_reg_node_desc out;
	int err;

	if (mask & ~IB_DEVICE_MODIFY_NODE_DESC)
		return -EOPNOTSUPP;

	if (!(mask & IB_DEVICE_MODIFY_NODE_DESC))
		return 0;

	/*
	 * If possible, pass node desc to FW, so it can generate
	 * a 144 trap.  If cmd fails, just ignore.
	 */
	memcpy(&in, props->node_desc, IB_DEVICE_NODE_DESC_MAX);
	err = mlx5_core_access_reg(dev->mdev, &in, sizeof(in), &out,
				   sizeof(out), MLX5_REG_NODE_DESC, 0, 1);
	if (err)
		return err;

	memcpy(ibdev->node_desc, props->node_desc, IB_DEVICE_NODE_DESC_MAX);

	return err;
}

static int set_port_caps_atomic(struct mlx5_ib_dev *dev, u32 port_num, u32 mask,
				u32 value)
{
	struct mlx5_hca_vport_context ctx = {};
	struct mlx5_core_dev *mdev;
	u32 mdev_port_num;
	int err;

	mdev = mlx5_ib_get_native_port_mdev(dev, port_num, &mdev_port_num);
	if (!mdev)
		return -ENODEV;

	err = mlx5_query_hca_vport_context(mdev, 0, mdev_port_num, 0, &ctx);
	if (err)
		goto out;

	if (~ctx.cap_mask1_perm & mask) {
		mlx5_ib_warn(dev, "trying to change bitmask 0x%X but change supported 0x%X\n",
			     mask, ctx.cap_mask1_perm);
		err = -EINVAL;
		goto out;
	}

	ctx.cap_mask1 = value;
	ctx.cap_mask1_perm = mask;
	err = mlx5_core_modify_hca_vport_context(mdev, 0, mdev_port_num,
						 0, &ctx);

out:
	mlx5_ib_put_native_port_mdev(dev, port_num);

	return err;
}

static int mlx5_ib_modify_port(struct ib_device *ibdev, u32 port, int mask,
			       struct ib_port_modify *props)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct ib_port_attr attr;
	u32 tmp;
	int err;
	u32 change_mask;
	u32 value;
	bool is_ib = (mlx5_ib_port_link_layer(ibdev, port) ==
		      IB_LINK_LAYER_INFINIBAND);

	/* CM layer calls ib_modify_port() regardless of the link layer. For
	 * Ethernet ports, qkey violation and Port capabilities are meaningless.
	 */
	if (!is_ib)
		return 0;

	if (MLX5_CAP_GEN(dev->mdev, ib_virt) && is_ib) {
		change_mask = props->clr_port_cap_mask | props->set_port_cap_mask;
		value = ~props->clr_port_cap_mask | props->set_port_cap_mask;
		return set_port_caps_atomic(dev, port, change_mask, value);
	}

	mutex_lock(&dev->cap_mask_mutex);

	err = ib_query_port(ibdev, port, &attr);
	if (err)
		goto out;

	tmp = (attr.port_cap_flags | props->set_port_cap_mask) &
		~props->clr_port_cap_mask;

	err = mlx5_set_port_caps(dev->mdev, port, tmp);

out:
	mutex_unlock(&dev->cap_mask_mutex);
	return err;
}

static void print_lib_caps(struct mlx5_ib_dev *dev, u64 caps)
{
	mlx5_ib_dbg(dev, "MLX5_LIB_CAP_4K_UAR = %s\n",
		    caps & MLX5_LIB_CAP_4K_UAR ? "y" : "n");
}

static u16 calc_dynamic_bfregs(int uars_per_sys_page)
{
	/* Large page with non 4k uar support might limit the dynamic size */
	if (uars_per_sys_page == 1  && PAGE_SIZE > 4096)
		return MLX5_MIN_DYN_BFREGS;

	return MLX5_MAX_DYN_BFREGS;
}

static int calc_total_bfregs(struct mlx5_ib_dev *dev, bool lib_uar_4k,
			     struct mlx5_ib_alloc_ucontext_req_v2 *req,
			     struct mlx5_bfreg_info *bfregi)
{
	int uars_per_sys_page;
	int bfregs_per_sys_page;
	int ref_bfregs = req->total_num_bfregs;

	if (req->total_num_bfregs == 0)
		return -EINVAL;

	BUILD_BUG_ON(MLX5_MAX_BFREGS % MLX5_NON_FP_BFREGS_IN_PAGE);
	BUILD_BUG_ON(MLX5_MAX_BFREGS < MLX5_NON_FP_BFREGS_IN_PAGE);

	if (req->total_num_bfregs > MLX5_MAX_BFREGS)
		return -ENOMEM;

	uars_per_sys_page = get_uars_per_sys_page(dev, lib_uar_4k);
	bfregs_per_sys_page = uars_per_sys_page * MLX5_NON_FP_BFREGS_PER_UAR;
	/* This holds the required static allocation asked by the user */
	req->total_num_bfregs = ALIGN(req->total_num_bfregs, bfregs_per_sys_page);
	if (req->num_low_latency_bfregs > req->total_num_bfregs - 1)
		return -EINVAL;

	bfregi->num_static_sys_pages = req->total_num_bfregs / bfregs_per_sys_page;
	bfregi->num_dyn_bfregs = ALIGN(calc_dynamic_bfregs(uars_per_sys_page), bfregs_per_sys_page);
	bfregi->total_num_bfregs = req->total_num_bfregs + bfregi->num_dyn_bfregs;
	bfregi->num_sys_pages = bfregi->total_num_bfregs / bfregs_per_sys_page;

	mlx5_ib_dbg(dev, "uar_4k: fw support %s, lib support %s, user requested %d bfregs, allocated %d, total bfregs %d, using %d sys pages\n",
		    MLX5_CAP_GEN(dev->mdev, uar_4k) ? "yes" : "no",
		    lib_uar_4k ? "yes" : "no", ref_bfregs,
		    req->total_num_bfregs, bfregi->total_num_bfregs,
		    bfregi->num_sys_pages);

	return 0;
}

static int allocate_uars(struct mlx5_ib_dev *dev, struct mlx5_ib_ucontext *context)
{
	struct mlx5_bfreg_info *bfregi;
	int err;
	int i;

	bfregi = &context->bfregi;
	for (i = 0; i < bfregi->num_static_sys_pages; i++) {
		err = mlx5_cmd_uar_alloc(dev->mdev, &bfregi->sys_pages[i],
					 context->devx_uid);
		if (err)
			goto error;

		mlx5_ib_dbg(dev, "allocated uar %d\n", bfregi->sys_pages[i]);
	}

	for (i = bfregi->num_static_sys_pages; i < bfregi->num_sys_pages; i++)
		bfregi->sys_pages[i] = MLX5_IB_INVALID_UAR_INDEX;

	return 0;

error:
	for (--i; i >= 0; i--)
		if (mlx5_cmd_uar_dealloc(dev->mdev, bfregi->sys_pages[i],
					 context->devx_uid))
			mlx5_ib_warn(dev, "failed to free uar %d\n", i);

	return err;
}

static void deallocate_uars(struct mlx5_ib_dev *dev,
			    struct mlx5_ib_ucontext *context)
{
	struct mlx5_bfreg_info *bfregi;
	int i;

	bfregi = &context->bfregi;
	for (i = 0; i < bfregi->num_sys_pages; i++)
		if (i < bfregi->num_static_sys_pages ||
		    bfregi->sys_pages[i] != MLX5_IB_INVALID_UAR_INDEX)
			mlx5_cmd_uar_dealloc(dev->mdev, bfregi->sys_pages[i],
					     context->devx_uid);
}

int mlx5_ib_enable_lb(struct mlx5_ib_dev *dev, bool td, bool qp)
{
	int err = 0;

	mutex_lock(&dev->lb.mutex);
	if (td)
		dev->lb.user_td++;
	if (qp)
		dev->lb.qps++;

	if (dev->lb.user_td == 2 ||
	    dev->lb.qps == 1) {
		if (!dev->lb.enabled) {
			err = mlx5_nic_vport_update_local_lb(dev->mdev, true);
			dev->lb.enabled = true;
		}
	}

	mutex_unlock(&dev->lb.mutex);

	return err;
}

void mlx5_ib_disable_lb(struct mlx5_ib_dev *dev, bool td, bool qp)
{
	mutex_lock(&dev->lb.mutex);
	if (td)
		dev->lb.user_td--;
	if (qp)
		dev->lb.qps--;

	if (dev->lb.user_td == 1 &&
	    dev->lb.qps == 0) {
		if (dev->lb.enabled) {
			mlx5_nic_vport_update_local_lb(dev->mdev, false);
			dev->lb.enabled = false;
		}
	}

	mutex_unlock(&dev->lb.mutex);
}

static int mlx5_ib_alloc_transport_domain(struct mlx5_ib_dev *dev, u32 *tdn,
					  u16 uid)
{
	int err;

	if (!MLX5_CAP_GEN(dev->mdev, log_max_transport_domain))
		return 0;

	err = mlx5_cmd_alloc_transport_domain(dev->mdev, tdn, uid);
	if (err)
		return err;

	if ((MLX5_CAP_GEN(dev->mdev, port_type) != MLX5_CAP_PORT_TYPE_ETH) ||
	    (!MLX5_CAP_GEN(dev->mdev, disable_local_lb_uc) &&
	     !MLX5_CAP_GEN(dev->mdev, disable_local_lb_mc)))
		return err;

	return mlx5_ib_enable_lb(dev, true, false);
}

static void mlx5_ib_dealloc_transport_domain(struct mlx5_ib_dev *dev, u32 tdn,
					     u16 uid)
{
	if (!MLX5_CAP_GEN(dev->mdev, log_max_transport_domain))
		return;

	mlx5_cmd_dealloc_transport_domain(dev->mdev, tdn, uid);

	if ((MLX5_CAP_GEN(dev->mdev, port_type) != MLX5_CAP_PORT_TYPE_ETH) ||
	    (!MLX5_CAP_GEN(dev->mdev, disable_local_lb_uc) &&
	     !MLX5_CAP_GEN(dev->mdev, disable_local_lb_mc)))
		return;

	mlx5_ib_disable_lb(dev, true, false);
}

static int set_ucontext_resp(struct ib_ucontext *uctx,
			     struct mlx5_ib_alloc_ucontext_resp *resp)
{
	struct ib_device *ibdev = uctx->device;
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_ib_ucontext *context = to_mucontext(uctx);
	struct mlx5_bfreg_info *bfregi = &context->bfregi;

	if (MLX5_CAP_GEN(dev->mdev, dump_fill_mkey)) {
		resp->dump_fill_mkey = dev->mkeys.dump_fill_mkey;
		resp->comp_mask |=
			MLX5_IB_ALLOC_UCONTEXT_RESP_MASK_DUMP_FILL_MKEY;
	}

	resp->qp_tab_size = 1 << MLX5_CAP_GEN(dev->mdev, log_max_qp);
	if (mlx5_wc_support_get(dev->mdev))
		resp->bf_reg_size = 1 << MLX5_CAP_GEN(dev->mdev,
						      log_bf_reg_size);
	resp->cache_line_size = cache_line_size();
	resp->max_sq_desc_sz = MLX5_CAP_GEN(dev->mdev, max_wqe_sz_sq);
	resp->max_rq_desc_sz = MLX5_CAP_GEN(dev->mdev, max_wqe_sz_rq);
	resp->max_send_wqebb = 1 << MLX5_CAP_GEN(dev->mdev, log_max_qp_sz);
	resp->max_recv_wr = 1 << MLX5_CAP_GEN(dev->mdev, log_max_qp_sz);
	resp->max_srq_recv_wr = 1 << MLX5_CAP_GEN(dev->mdev, log_max_srq_sz);
	resp->cqe_version = context->cqe_version;
	resp->log_uar_size = MLX5_CAP_GEN(dev->mdev, uar_4k) ?
				MLX5_ADAPTER_PAGE_SHIFT : PAGE_SHIFT;
	resp->num_uars_per_page = MLX5_CAP_GEN(dev->mdev, uar_4k) ?
					MLX5_CAP_GEN(dev->mdev,
						     num_of_uars_per_page) : 1;
	resp->tot_bfregs = bfregi->lib_uar_dyn ? 0 :
			bfregi->total_num_bfregs - bfregi->num_dyn_bfregs;
	resp->num_ports = dev->num_ports;
	resp->cmds_supp_uhw |= MLX5_USER_CMDS_SUPP_UHW_QUERY_DEVICE |
				      MLX5_USER_CMDS_SUPP_UHW_CREATE_AH;

	if (mlx5_ib_port_link_layer(ibdev, 1) == IB_LINK_LAYER_ETHERNET) {
		mlx5_query_min_inline(dev->mdev, &resp->eth_min_inline);
		resp->eth_min_inline++;
	}

	if (dev->mdev->clock_info)
		resp->clock_info_versions = BIT(MLX5_IB_CLOCK_INFO_V1);

	/*
	 * We don't want to expose information from the PCI bar that is located
	 * after 4096 bytes, so if the arch only supports larger pages, let's
	 * pretend we don't support reading the HCA's core clock. This is also
	 * forced by mmap function.
	 */
	if (PAGE_SIZE <= 4096) {
		resp->comp_mask |=
			MLX5_IB_ALLOC_UCONTEXT_RESP_MASK_CORE_CLOCK_OFFSET;
		resp->hca_core_clock_offset =
			offsetof(struct mlx5_init_seg,
				 internal_timer_h) % PAGE_SIZE;
	}

	if (MLX5_CAP_GEN(dev->mdev, ece_support))
		resp->comp_mask |= MLX5_IB_ALLOC_UCONTEXT_RESP_MASK_ECE;

	if (rt_supported(MLX5_CAP_GEN(dev->mdev, sq_ts_format)) &&
	    rt_supported(MLX5_CAP_GEN(dev->mdev, rq_ts_format)) &&
	    rt_supported(MLX5_CAP_ROCE(dev->mdev, qp_ts_format)))
		resp->comp_mask |=
			MLX5_IB_ALLOC_UCONTEXT_RESP_MASK_REAL_TIME_TS;

	resp->num_dyn_bfregs = bfregi->num_dyn_bfregs;

	if (MLX5_CAP_GEN(dev->mdev, drain_sigerr))
		resp->comp_mask |= MLX5_IB_ALLOC_UCONTEXT_RESP_MASK_SQD2RTS;

	resp->comp_mask |=
		MLX5_IB_ALLOC_UCONTEXT_RESP_MASK_MKEY_UPDATE_TAG;

	return 0;
}

static int mlx5_ib_alloc_ucontext(struct ib_ucontext *uctx,
				  struct ib_udata *udata)
{
	struct ib_device *ibdev = uctx->device;
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	struct mlx5_ib_alloc_ucontext_req_v2 req = {};
	struct mlx5_ib_alloc_ucontext_resp resp = {};
	struct mlx5_ib_ucontext *context = to_mucontext(uctx);
	struct mlx5_bfreg_info *bfregi;
	int ver;
	int err;
	size_t min_req_v2 = offsetof(struct mlx5_ib_alloc_ucontext_req_v2,
				     max_cqe_version);
	bool lib_uar_4k;
	bool lib_uar_dyn;

	if (!dev->ib_active)
		return -EAGAIN;

	if (udata->inlen == sizeof(struct mlx5_ib_alloc_ucontext_req))
		ver = 0;
	else if (udata->inlen >= min_req_v2)
		ver = 2;
	else
		return -EINVAL;

	err = ib_copy_from_udata(&req, udata, min(udata->inlen, sizeof(req)));
	if (err)
		return err;

	if (req.flags & ~MLX5_IB_ALLOC_UCTX_DEVX)
		return -EOPNOTSUPP;

	if (req.comp_mask || req.reserved0 || req.reserved1 || req.reserved2)
		return -EOPNOTSUPP;

	req.total_num_bfregs = ALIGN(req.total_num_bfregs,
				    MLX5_NON_FP_BFREGS_PER_UAR);
	if (req.num_low_latency_bfregs > req.total_num_bfregs - 1)
		return -EINVAL;

	if (req.flags & MLX5_IB_ALLOC_UCTX_DEVX) {
		err = mlx5_ib_devx_create(dev, true);
		if (err < 0)
			goto out_ctx;
		context->devx_uid = err;
	}

	lib_uar_4k = req.lib_caps & MLX5_LIB_CAP_4K_UAR;
	lib_uar_dyn = req.lib_caps & MLX5_LIB_CAP_DYN_UAR;
	bfregi = &context->bfregi;

	if (lib_uar_dyn) {
		bfregi->lib_uar_dyn = lib_uar_dyn;
		goto uar_done;
	}

	/* updates req->total_num_bfregs */
	err = calc_total_bfregs(dev, lib_uar_4k, &req, bfregi);
	if (err)
		goto out_devx;

	mutex_init(&bfregi->lock);
	bfregi->lib_uar_4k = lib_uar_4k;
	bfregi->count = kcalloc(bfregi->total_num_bfregs, sizeof(*bfregi->count),
				GFP_KERNEL);
	if (!bfregi->count) {
		err = -ENOMEM;
		goto out_devx;
	}

	bfregi->sys_pages = kcalloc(bfregi->num_sys_pages,
				    sizeof(*bfregi->sys_pages),
				    GFP_KERNEL);
	if (!bfregi->sys_pages) {
		err = -ENOMEM;
		goto out_count;
	}

	err = allocate_uars(dev, context);
	if (err)
		goto out_sys_pages;

uar_done:
	err = mlx5_ib_alloc_transport_domain(dev, &context->tdn,
					     context->devx_uid);
	if (err)
		goto out_uars;

	INIT_LIST_HEAD(&context->db_page_list);
	mutex_init(&context->db_page_mutex);

	context->cqe_version = min_t(__u8,
				 (__u8)MLX5_CAP_GEN(dev->mdev, cqe_version),
				 req.max_cqe_version);

	err = set_ucontext_resp(uctx, &resp);
	if (err)
		goto out_mdev;

	resp.response_length = min(udata->outlen, sizeof(resp));
	err = ib_copy_to_udata(udata, &resp, resp.response_length);
	if (err)
		goto out_mdev;

	bfregi->ver = ver;
	bfregi->num_low_latency_bfregs = req.num_low_latency_bfregs;
	context->lib_caps = req.lib_caps;
	print_lib_caps(dev, context->lib_caps);

	if (mlx5_ib_lag_should_assign_affinity(dev)) {
		u32 port = mlx5_core_native_port_num(dev->mdev) - 1;

		atomic_set(&context->tx_port_affinity,
			   atomic_add_return(
				   1, &dev->port[port].roce.tx_port_affinity));
	}

	return 0;

out_mdev:
	mlx5_ib_dealloc_transport_domain(dev, context->tdn, context->devx_uid);

out_uars:
	deallocate_uars(dev, context);

out_sys_pages:
	kfree(bfregi->sys_pages);

out_count:
	kfree(bfregi->count);

out_devx:
	if (req.flags & MLX5_IB_ALLOC_UCTX_DEVX)
		mlx5_ib_devx_destroy(dev, context->devx_uid);

out_ctx:
	return err;
}

static int mlx5_ib_query_ucontext(struct ib_ucontext *ibcontext,
				  struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_alloc_ucontext_resp uctx_resp = {};
	int ret;

	ret = set_ucontext_resp(ibcontext, &uctx_resp);
	if (ret)
		return ret;

	uctx_resp.response_length =
		min_t(size_t,
		      uverbs_attr_get_len(attrs,
				MLX5_IB_ATTR_QUERY_CONTEXT_RESP_UCTX),
		      sizeof(uctx_resp));

	ret = uverbs_copy_to_struct_or_zero(attrs,
					MLX5_IB_ATTR_QUERY_CONTEXT_RESP_UCTX,
					&uctx_resp,
					sizeof(uctx_resp));
	return ret;
}

static void mlx5_ib_dealloc_ucontext(struct ib_ucontext *ibcontext)
{
	struct mlx5_ib_ucontext *context = to_mucontext(ibcontext);
	struct mlx5_ib_dev *dev = to_mdev(ibcontext->device);
	struct mlx5_bfreg_info *bfregi;

	bfregi = &context->bfregi;
	mlx5_ib_dealloc_transport_domain(dev, context->tdn, context->devx_uid);

	deallocate_uars(dev, context);
	kfree(bfregi->sys_pages);
	kfree(bfregi->count);

	if (context->devx_uid)
		mlx5_ib_devx_destroy(dev, context->devx_uid);
}

static phys_addr_t uar_index2pfn(struct mlx5_ib_dev *dev,
				 int uar_idx)
{
	int fw_uars_per_page;

	fw_uars_per_page = MLX5_CAP_GEN(dev->mdev, uar_4k) ? MLX5_UARS_IN_PAGE : 1;

	return (dev->mdev->bar_addr >> PAGE_SHIFT) + uar_idx / fw_uars_per_page;
}

static u64 uar_index2paddress(struct mlx5_ib_dev *dev,
				 int uar_idx)
{
	unsigned int fw_uars_per_page;

	fw_uars_per_page = MLX5_CAP_GEN(dev->mdev, uar_4k) ?
				MLX5_UARS_IN_PAGE : 1;

	return (dev->mdev->bar_addr + (uar_idx / fw_uars_per_page) * PAGE_SIZE);
}

static int get_command(unsigned long offset)
{
	return (offset >> MLX5_IB_MMAP_CMD_SHIFT) & MLX5_IB_MMAP_CMD_MASK;
}

static int get_arg(unsigned long offset)
{
	return offset & ((1 << MLX5_IB_MMAP_CMD_SHIFT) - 1);
}

static int get_index(unsigned long offset)
{
	return get_arg(offset);
}

/* Index resides in an extra byte to enable larger values than 255 */
static int get_extended_index(unsigned long offset)
{
	return get_arg(offset) | ((offset >> 16) & 0xff) << 8;
}


static void mlx5_ib_disassociate_ucontext(struct ib_ucontext *ibcontext)
{
}

static inline char *mmap_cmd2str(enum mlx5_ib_mmap_cmd cmd)
{
	switch (cmd) {
	case MLX5_IB_MMAP_WC_PAGE:
		return "WC";
	case MLX5_IB_MMAP_REGULAR_PAGE:
		return "best effort WC";
	case MLX5_IB_MMAP_NC_PAGE:
		return "NC";
	case MLX5_IB_MMAP_DEVICE_MEM:
		return "Device Memory";
	default:
		return "Unknown";
	}
}

static int mlx5_ib_mmap_clock_info_page(struct mlx5_ib_dev *dev,
					struct vm_area_struct *vma,
					struct mlx5_ib_ucontext *context)
{
	if ((vma->vm_end - vma->vm_start != PAGE_SIZE) ||
	    !(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	if (get_index(vma->vm_pgoff) != MLX5_IB_CLOCK_INFO_V1)
		return -EOPNOTSUPP;

	if (vma->vm_flags & (VM_WRITE | VM_EXEC))
		return -EPERM;
	vm_flags_clear(vma, VM_MAYWRITE);

	if (!dev->mdev->clock_info)
		return -EOPNOTSUPP;

	return vm_insert_page(vma, vma->vm_start,
			      virt_to_page(dev->mdev->clock_info));
}

static void mlx5_ib_mmap_free(struct rdma_user_mmap_entry *entry)
{
	struct mlx5_user_mmap_entry *mentry = to_mmmap(entry);
	struct mlx5_ib_dev *dev = to_mdev(entry->ucontext->device);
	struct mlx5_var_table *var_table = &dev->var_table;
	struct mlx5_ib_ucontext *context = to_mucontext(entry->ucontext);

	switch (mentry->mmap_flag) {
	case MLX5_IB_MMAP_TYPE_MEMIC:
	case MLX5_IB_MMAP_TYPE_MEMIC_OP:
		mlx5_ib_dm_mmap_free(dev, mentry);
		break;
	case MLX5_IB_MMAP_TYPE_VAR:
		mutex_lock(&var_table->bitmap_lock);
		clear_bit(mentry->page_idx, var_table->bitmap);
		mutex_unlock(&var_table->bitmap_lock);
		kfree(mentry);
		break;
	case MLX5_IB_MMAP_TYPE_UAR_WC:
	case MLX5_IB_MMAP_TYPE_UAR_NC:
		mlx5_cmd_uar_dealloc(dev->mdev, mentry->page_idx,
				     context->devx_uid);
		kfree(mentry);
		break;
	default:
		WARN_ON(true);
	}
}

static int uar_mmap(struct mlx5_ib_dev *dev, enum mlx5_ib_mmap_cmd cmd,
		    struct vm_area_struct *vma,
		    struct mlx5_ib_ucontext *context)
{
	struct mlx5_bfreg_info *bfregi = &context->bfregi;
	int err;
	unsigned long idx;
	phys_addr_t pfn;
	pgprot_t prot;
	u32 bfreg_dyn_idx = 0;
	u32 uar_index;
	int dyn_uar = (cmd == MLX5_IB_MMAP_ALLOC_WC);
	int max_valid_idx = dyn_uar ? bfregi->num_sys_pages :
				bfregi->num_static_sys_pages;

	if (bfregi->lib_uar_dyn)
		return -EINVAL;

	if (vma->vm_end - vma->vm_start != PAGE_SIZE)
		return -EINVAL;

	if (dyn_uar)
		idx = get_extended_index(vma->vm_pgoff) + bfregi->num_static_sys_pages;
	else
		idx = get_index(vma->vm_pgoff);

	if (idx >= max_valid_idx) {
		mlx5_ib_warn(dev, "invalid uar index %lu, max=%d\n",
			     idx, max_valid_idx);
		return -EINVAL;
	}

	switch (cmd) {
	case MLX5_IB_MMAP_WC_PAGE:
	case MLX5_IB_MMAP_ALLOC_WC:
	case MLX5_IB_MMAP_REGULAR_PAGE:
		/* For MLX5_IB_MMAP_REGULAR_PAGE do the best effort to get WC */
		prot = pgprot_writecombine(vma->vm_page_prot);
		break;
	case MLX5_IB_MMAP_NC_PAGE:
		prot = pgprot_noncached(vma->vm_page_prot);
		break;
	default:
		return -EINVAL;
	}

	if (dyn_uar) {
		int uars_per_page;

		uars_per_page = get_uars_per_sys_page(dev, bfregi->lib_uar_4k);
		bfreg_dyn_idx = idx * (uars_per_page * MLX5_NON_FP_BFREGS_PER_UAR);
		if (bfreg_dyn_idx >= bfregi->total_num_bfregs) {
			mlx5_ib_warn(dev, "invalid bfreg_dyn_idx %u, max=%u\n",
				     bfreg_dyn_idx, bfregi->total_num_bfregs);
			return -EINVAL;
		}

		mutex_lock(&bfregi->lock);
		/* Fail if uar already allocated, first bfreg index of each
		 * page holds its count.
		 */
		if (bfregi->count[bfreg_dyn_idx]) {
			mlx5_ib_warn(dev, "wrong offset, idx %lu is busy, bfregn=%u\n", idx, bfreg_dyn_idx);
			mutex_unlock(&bfregi->lock);
			return -EINVAL;
		}

		bfregi->count[bfreg_dyn_idx]++;
		mutex_unlock(&bfregi->lock);

		err = mlx5_cmd_uar_alloc(dev->mdev, &uar_index,
					 context->devx_uid);
		if (err) {
			mlx5_ib_warn(dev, "UAR alloc failed\n");
			goto free_bfreg;
		}
	} else {
		uar_index = bfregi->sys_pages[idx];
	}

	pfn = uar_index2pfn(dev, uar_index);
	mlx5_ib_dbg(dev, "uar idx 0x%lx, pfn %pa\n", idx, &pfn);

	err = rdma_user_mmap_io(&context->ibucontext, vma, pfn, PAGE_SIZE,
				prot, NULL);
	if (err) {
		mlx5_ib_err(dev,
			    "rdma_user_mmap_io failed with error=%d, mmap_cmd=%s\n",
			    err, mmap_cmd2str(cmd));
		goto err;
	}

	if (dyn_uar)
		bfregi->sys_pages[idx] = uar_index;
	return 0;

err:
	if (!dyn_uar)
		return err;

	mlx5_cmd_uar_dealloc(dev->mdev, idx, context->devx_uid);

free_bfreg:
	mlx5_ib_free_bfreg(dev, bfregi, bfreg_dyn_idx);

	return err;
}

static unsigned long mlx5_vma_to_pgoff(struct vm_area_struct *vma)
{
	unsigned long idx;
	u8 command;

	command = get_command(vma->vm_pgoff);
	idx = get_extended_index(vma->vm_pgoff);

	return (command << 16 | idx);
}

static int mlx5_ib_mmap_offset(struct mlx5_ib_dev *dev,
			       struct vm_area_struct *vma,
			       struct ib_ucontext *ucontext)
{
	struct mlx5_user_mmap_entry *mentry;
	struct rdma_user_mmap_entry *entry;
	unsigned long pgoff;
	pgprot_t prot;
	phys_addr_t pfn;
	int ret;

	pgoff = mlx5_vma_to_pgoff(vma);
	entry = rdma_user_mmap_entry_get_pgoff(ucontext, pgoff);
	if (!entry)
		return -EINVAL;

	mentry = to_mmmap(entry);
	pfn = (mentry->address >> PAGE_SHIFT);
	if (mentry->mmap_flag == MLX5_IB_MMAP_TYPE_VAR ||
	    mentry->mmap_flag == MLX5_IB_MMAP_TYPE_UAR_NC)
		prot = pgprot_noncached(vma->vm_page_prot);
	else
		prot = pgprot_writecombine(vma->vm_page_prot);
	ret = rdma_user_mmap_io(ucontext, vma, pfn,
				entry->npages * PAGE_SIZE,
				prot,
				entry);
	rdma_user_mmap_entry_put(&mentry->rdma_entry);
	return ret;
}

static u64 mlx5_entry_to_mmap_offset(struct mlx5_user_mmap_entry *entry)
{
	u64 cmd = (entry->rdma_entry.start_pgoff >> 16) & 0xFFFF;
	u64 index = entry->rdma_entry.start_pgoff & 0xFFFF;

	return (((index >> 8) << 16) | (cmd << MLX5_IB_MMAP_CMD_SHIFT) |
		(index & 0xFF)) << PAGE_SHIFT;
}

static int mlx5_ib_mmap(struct ib_ucontext *ibcontext, struct vm_area_struct *vma)
{
	struct mlx5_ib_ucontext *context = to_mucontext(ibcontext);
	struct mlx5_ib_dev *dev = to_mdev(ibcontext->device);
	unsigned long command;
	phys_addr_t pfn;

	command = get_command(vma->vm_pgoff);
	switch (command) {
	case MLX5_IB_MMAP_WC_PAGE:
	case MLX5_IB_MMAP_ALLOC_WC:
		if (!mlx5_wc_support_get(dev->mdev))
			return -EPERM;
		fallthrough;
	case MLX5_IB_MMAP_NC_PAGE:
	case MLX5_IB_MMAP_REGULAR_PAGE:
		return uar_mmap(dev, command, vma, context);

	case MLX5_IB_MMAP_GET_CONTIGUOUS_PAGES:
		return -ENOSYS;

	case MLX5_IB_MMAP_CORE_CLOCK:
		if (vma->vm_end - vma->vm_start != PAGE_SIZE)
			return -EINVAL;

		if (vma->vm_flags & VM_WRITE)
			return -EPERM;
		vm_flags_clear(vma, VM_MAYWRITE);

		/* Don't expose to user-space information it shouldn't have */
		if (PAGE_SIZE > 4096)
			return -EOPNOTSUPP;

		pfn = (dev->mdev->iseg_base +
		       offsetof(struct mlx5_init_seg, internal_timer_h)) >>
			PAGE_SHIFT;
		return rdma_user_mmap_io(&context->ibucontext, vma, pfn,
					 PAGE_SIZE,
					 pgprot_noncached(vma->vm_page_prot),
					 NULL);
	case MLX5_IB_MMAP_CLOCK_INFO:
		return mlx5_ib_mmap_clock_info_page(dev, vma, context);

	default:
		return mlx5_ib_mmap_offset(dev, vma, ibcontext);
	}

	return 0;
}

static int mlx5_ib_alloc_pd(struct ib_pd *ibpd, struct ib_udata *udata)
{
	struct mlx5_ib_pd *pd = to_mpd(ibpd);
	struct ib_device *ibdev = ibpd->device;
	struct mlx5_ib_alloc_pd_resp resp;
	int err;
	u32 out[MLX5_ST_SZ_DW(alloc_pd_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_pd_in)] = {};
	u16 uid = 0;
	struct mlx5_ib_ucontext *context = rdma_udata_to_drv_context(
		udata, struct mlx5_ib_ucontext, ibucontext);

	uid = context ? context->devx_uid : 0;
	MLX5_SET(alloc_pd_in, in, opcode, MLX5_CMD_OP_ALLOC_PD);
	MLX5_SET(alloc_pd_in, in, uid, uid);
	err = mlx5_cmd_exec_inout(to_mdev(ibdev)->mdev, alloc_pd, in, out);
	if (err)
		return err;

	pd->pdn = MLX5_GET(alloc_pd_out, out, pd);
	pd->uid = uid;
	if (udata) {
		resp.pdn = pd->pdn;
		if (ib_copy_to_udata(udata, &resp, sizeof(resp))) {
			mlx5_cmd_dealloc_pd(to_mdev(ibdev)->mdev, pd->pdn, uid);
			return -EFAULT;
		}
	}

	return 0;
}

static int mlx5_ib_dealloc_pd(struct ib_pd *pd, struct ib_udata *udata)
{
	struct mlx5_ib_dev *mdev = to_mdev(pd->device);
	struct mlx5_ib_pd *mpd = to_mpd(pd);

	return mlx5_cmd_dealloc_pd(mdev->mdev, mpd->pdn, mpd->uid);
}

static int mlx5_ib_mcg_attach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	struct mlx5_ib_qp *mqp = to_mqp(ibqp);
	int err;
	u16 uid;

	uid = ibqp->pd ?
		to_mpd(ibqp->pd)->uid : 0;

	if (mqp->flags & IB_QP_CREATE_SOURCE_QPN) {
		mlx5_ib_dbg(dev, "Attaching a multi cast group to underlay QP is not supported\n");
		return -EOPNOTSUPP;
	}

	err = mlx5_cmd_attach_mcg(dev->mdev, gid, ibqp->qp_num, uid);
	if (err)
		mlx5_ib_warn(dev, "failed attaching QPN 0x%x, MGID %pI6\n",
			     ibqp->qp_num, gid->raw);

	return err;
}

static int mlx5_ib_mcg_detach(struct ib_qp *ibqp, union ib_gid *gid, u16 lid)
{
	struct mlx5_ib_dev *dev = to_mdev(ibqp->device);
	int err;
	u16 uid;

	uid = ibqp->pd ?
		to_mpd(ibqp->pd)->uid : 0;
	err = mlx5_cmd_detach_mcg(dev->mdev, gid, ibqp->qp_num, uid);
	if (err)
		mlx5_ib_warn(dev, "failed detaching QPN 0x%x, MGID %pI6\n",
			     ibqp->qp_num, gid->raw);

	return err;
}

static int init_node_data(struct mlx5_ib_dev *dev)
{
	int err;

	err = mlx5_query_node_desc(dev, dev->ib_dev.node_desc);
	if (err)
		return err;

	dev->mdev->rev_id = dev->mdev->pdev->revision;

	return mlx5_query_node_guid(dev, &dev->ib_dev.node_guid);
}

static ssize_t fw_pages_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct mlx5_ib_dev *dev =
		rdma_device_to_drv_device(device, struct mlx5_ib_dev, ib_dev);

	return sysfs_emit(buf, "%d\n", dev->mdev->priv.fw_pages);
}
static DEVICE_ATTR_RO(fw_pages);

static ssize_t reg_pages_show(struct device *device,
			      struct device_attribute *attr, char *buf)
{
	struct mlx5_ib_dev *dev =
		rdma_device_to_drv_device(device, struct mlx5_ib_dev, ib_dev);

	return sysfs_emit(buf, "%d\n", atomic_read(&dev->mdev->priv.reg_pages));
}
static DEVICE_ATTR_RO(reg_pages);

static ssize_t hca_type_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct mlx5_ib_dev *dev =
		rdma_device_to_drv_device(device, struct mlx5_ib_dev, ib_dev);

	return sysfs_emit(buf, "MT%d\n", dev->mdev->pdev->device);
}
static DEVICE_ATTR_RO(hca_type);

static ssize_t hw_rev_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	struct mlx5_ib_dev *dev =
		rdma_device_to_drv_device(device, struct mlx5_ib_dev, ib_dev);

	return sysfs_emit(buf, "%x\n", dev->mdev->rev_id);
}
static DEVICE_ATTR_RO(hw_rev);

static ssize_t board_id_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct mlx5_ib_dev *dev =
		rdma_device_to_drv_device(device, struct mlx5_ib_dev, ib_dev);

	return sysfs_emit(buf, "%.*s\n", MLX5_BOARD_ID_LEN,
			  dev->mdev->board_id);
}
static DEVICE_ATTR_RO(board_id);

static struct attribute *mlx5_class_attributes[] = {
	&dev_attr_hw_rev.attr,
	&dev_attr_hca_type.attr,
	&dev_attr_board_id.attr,
	&dev_attr_fw_pages.attr,
	&dev_attr_reg_pages.attr,
	NULL,
};

static const struct attribute_group mlx5_attr_group = {
	.attrs = mlx5_class_attributes,
};

static void pkey_change_handler(struct work_struct *work)
{
	struct mlx5_ib_port_resources *ports =
		container_of(work, struct mlx5_ib_port_resources,
			     pkey_change_work);

	if (!ports->gsi)
		/*
		 * We got this event before device was fully configured
		 * and MAD registration code wasn't called/finished yet.
		 */
		return;

	mlx5_ib_gsi_pkey_change(ports->gsi);
}

static void mlx5_ib_handle_internal_error(struct mlx5_ib_dev *ibdev)
{
	struct mlx5_ib_qp *mqp;
	struct mlx5_ib_cq *send_mcq, *recv_mcq;
	struct mlx5_core_cq *mcq;
	struct list_head cq_armed_list;
	unsigned long flags_qp;
	unsigned long flags_cq;
	unsigned long flags;

	INIT_LIST_HEAD(&cq_armed_list);

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
						      &cq_armed_list);
				}
			}
			spin_unlock_irqrestore(&send_mcq->lock, flags_cq);
		}
		spin_unlock_irqrestore(&mqp->sq.lock, flags_qp);
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
							      &cq_armed_list);
					}
				}
				spin_unlock_irqrestore(&recv_mcq->lock,
						       flags_cq);
			}
		}
		spin_unlock_irqrestore(&mqp->rq.lock, flags_qp);
	}
	/*At that point all inflight post send were put to be executed as of we
	 * lock/unlock above locks Now need to arm all involved CQs.
	 */
	list_for_each_entry(mcq, &cq_armed_list, reset_notify) {
		mcq->comp(mcq, NULL);
	}
	spin_unlock_irqrestore(&ibdev->reset_flow_resource_lock, flags);
}

static void delay_drop_handler(struct work_struct *work)
{
	int err;
	struct mlx5_ib_delay_drop *delay_drop =
		container_of(work, struct mlx5_ib_delay_drop,
			     delay_drop_work);

	atomic_inc(&delay_drop->events_cnt);

	mutex_lock(&delay_drop->lock);
	err = mlx5_core_set_delay_drop(delay_drop->dev, delay_drop->timeout);
	if (err) {
		mlx5_ib_warn(delay_drop->dev, "Failed to set delay drop, timeout=%u\n",
			     delay_drop->timeout);
		delay_drop->activate = false;
	}
	mutex_unlock(&delay_drop->lock);
}

static void handle_general_event(struct mlx5_ib_dev *ibdev, struct mlx5_eqe *eqe,
				 struct ib_event *ibev)
{
	u32 port = (eqe->data.port.port >> 4) & 0xf;

	switch (eqe->sub_type) {
	case MLX5_GENERAL_SUBTYPE_DELAY_DROP_TIMEOUT:
		if (mlx5_ib_port_link_layer(&ibdev->ib_dev, port) ==
					    IB_LINK_LAYER_ETHERNET)
			schedule_work(&ibdev->delay_drop.delay_drop_work);
		break;
	default: /* do nothing */
		return;
	}
}

static int handle_port_change(struct mlx5_ib_dev *ibdev, struct mlx5_eqe *eqe,
			      struct ib_event *ibev)
{
	u32 port = (eqe->data.port.port >> 4) & 0xf;

	ibev->element.port_num = port;

	switch (eqe->sub_type) {
	case MLX5_PORT_CHANGE_SUBTYPE_ACTIVE:
	case MLX5_PORT_CHANGE_SUBTYPE_DOWN:
	case MLX5_PORT_CHANGE_SUBTYPE_INITIALIZED:
		/* In RoCE, port up/down events are handled in
		 * mlx5_netdev_event().
		 */
		if (mlx5_ib_port_link_layer(&ibdev->ib_dev, port) ==
					    IB_LINK_LAYER_ETHERNET)
			return -EINVAL;

		ibev->event = (eqe->sub_type == MLX5_PORT_CHANGE_SUBTYPE_ACTIVE) ?
				IB_EVENT_PORT_ACTIVE : IB_EVENT_PORT_ERR;
		break;

	case MLX5_PORT_CHANGE_SUBTYPE_LID:
		ibev->event = IB_EVENT_LID_CHANGE;
		break;

	case MLX5_PORT_CHANGE_SUBTYPE_PKEY:
		ibev->event = IB_EVENT_PKEY_CHANGE;
		schedule_work(&ibdev->devr.ports[port - 1].pkey_change_work);
		break;

	case MLX5_PORT_CHANGE_SUBTYPE_GUID:
		ibev->event = IB_EVENT_GID_CHANGE;
		break;

	case MLX5_PORT_CHANGE_SUBTYPE_CLIENT_REREG:
		ibev->event = IB_EVENT_CLIENT_REREGISTER;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void mlx5_ib_handle_event(struct work_struct *_work)
{
	struct mlx5_ib_event_work *work =
		container_of(_work, struct mlx5_ib_event_work, work);
	struct mlx5_ib_dev *ibdev;
	struct ib_event ibev;
	bool fatal = false;

	if (work->is_slave) {
		ibdev = mlx5_ib_get_ibdev_from_mpi(work->mpi);
		if (!ibdev)
			goto out;
	} else {
		ibdev = work->dev;
	}

	switch (work->event) {
	case MLX5_DEV_EVENT_SYS_ERROR:
		ibev.event = IB_EVENT_DEVICE_FATAL;
		mlx5_ib_handle_internal_error(ibdev);
		ibev.element.port_num  = (u8)(unsigned long)work->param;
		fatal = true;
		break;
	case MLX5_EVENT_TYPE_PORT_CHANGE:
		if (handle_port_change(ibdev, work->param, &ibev))
			goto out;
		break;
	case MLX5_EVENT_TYPE_GENERAL_EVENT:
		handle_general_event(ibdev, work->param, &ibev);
		fallthrough;
	default:
		goto out;
	}

	ibev.device = &ibdev->ib_dev;

	if (!rdma_is_port_valid(&ibdev->ib_dev, ibev.element.port_num)) {
		mlx5_ib_warn(ibdev, "warning: event on port %d\n",  ibev.element.port_num);
		goto out;
	}

	if (ibdev->ib_active)
		ib_dispatch_event(&ibev);

	if (fatal)
		ibdev->ib_active = false;
out:
	kfree(work);
}

static int mlx5_ib_event(struct notifier_block *nb,
			 unsigned long event, void *param)
{
	struct mlx5_ib_event_work *work;

	work = kmalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return NOTIFY_DONE;

	INIT_WORK(&work->work, mlx5_ib_handle_event);
	work->dev = container_of(nb, struct mlx5_ib_dev, mdev_events);
	work->is_slave = false;
	work->param = param;
	work->event = event;

	queue_work(mlx5_ib_event_wq, &work->work);

	return NOTIFY_OK;
}

static int mlx5_ib_event_slave_port(struct notifier_block *nb,
				    unsigned long event, void *param)
{
	struct mlx5_ib_event_work *work;

	work = kmalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return NOTIFY_DONE;

	INIT_WORK(&work->work, mlx5_ib_handle_event);
	work->mpi = container_of(nb, struct mlx5_ib_multiport_info, mdev_events);
	work->is_slave = true;
	work->param = param;
	work->event = event;
	queue_work(mlx5_ib_event_wq, &work->work);

	return NOTIFY_OK;
}

static int mlx5_ib_get_plane_num(struct mlx5_core_dev *mdev, u8 *num_plane)
{
	struct mlx5_hca_vport_context vport_ctx;
	int err;

	*num_plane = 0;
	if (!MLX5_CAP_GEN(mdev, ib_virt) || !MLX5_CAP_GEN_2(mdev, multiplane))
		return 0;

	err = mlx5_query_hca_vport_context(mdev, 0, 1, 0, &vport_ctx);
	if (err)
		return err;

	*num_plane = vport_ctx.num_plane;
	return 0;
}

static int set_has_smi_cap(struct mlx5_ib_dev *dev)
{
	struct mlx5_hca_vport_context vport_ctx;
	int err;
	int port;

	if (MLX5_CAP_GEN(dev->mdev, port_type) != MLX5_CAP_PORT_TYPE_IB)
		return 0;

	for (port = 1; port <= dev->num_ports; port++) {
		if (dev->num_plane) {
			dev->port_caps[port - 1].has_smi = false;
			continue;
		} else if (!MLX5_CAP_GEN(dev->mdev, ib_virt) ||
			dev->ib_dev.type == RDMA_DEVICE_TYPE_SMI) {
			dev->port_caps[port - 1].has_smi = true;
			continue;
		}

		err = mlx5_query_hca_vport_context(dev->mdev, 0, port, 0,
						   &vport_ctx);
		if (err) {
			mlx5_ib_err(dev, "query_hca_vport_context for port=%d failed %d\n",
				    port, err);
			return err;
		}
		dev->port_caps[port - 1].has_smi = vport_ctx.has_smi;
	}

	return 0;
}

static void get_ext_port_caps(struct mlx5_ib_dev *dev)
{
	unsigned int port;

	rdma_for_each_port (&dev->ib_dev, port)
		mlx5_query_ext_port_caps(dev, port);
}

static u8 mlx5_get_umr_fence(u8 umr_fence_cap)
{
	switch (umr_fence_cap) {
	case MLX5_CAP_UMR_FENCE_NONE:
		return MLX5_FENCE_MODE_NONE;
	case MLX5_CAP_UMR_FENCE_SMALL:
		return MLX5_FENCE_MODE_INITIATOR_SMALL;
	default:
		return MLX5_FENCE_MODE_STRONG_ORDERING;
	}
}

int mlx5_ib_dev_res_cq_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_ib_resources *devr = &dev->devr;
	struct ib_cq_init_attr cq_attr = {.cqe = 1};
	struct ib_device *ibdev;
	struct ib_pd *pd;
	struct ib_cq *cq;
	int ret = 0;


	/*
	 * devr->c0 is set once, never changed until device unload.
	 * Avoid taking the mutex if initialization is already done.
	 */
	if (devr->c0)
		return 0;

	mutex_lock(&devr->cq_lock);
	if (devr->c0)
		goto unlock;

	ibdev = &dev->ib_dev;
	pd = ib_alloc_pd(ibdev, 0);
	if (IS_ERR(pd)) {
		ret = PTR_ERR(pd);
		mlx5_ib_err(dev, "Couldn't allocate PD for res init, err=%d\n", ret);
		goto unlock;
	}

	cq = ib_create_cq(ibdev, NULL, NULL, NULL, &cq_attr);
	if (IS_ERR(cq)) {
		ret = PTR_ERR(cq);
		mlx5_ib_err(dev, "Couldn't create CQ for res init, err=%d\n", ret);
		ib_dealloc_pd(pd);
		goto unlock;
	}

	devr->p0 = pd;
	devr->c0 = cq;

unlock:
	mutex_unlock(&devr->cq_lock);
	return ret;
}

int mlx5_ib_dev_res_srq_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_ib_resources *devr = &dev->devr;
	struct ib_srq_init_attr attr;
	struct ib_srq *s0, *s1;
	int ret = 0;

	/*
	 * devr->s1 is set once, never changed until device unload.
	 * Avoid taking the mutex if initialization is already done.
	 */
	if (devr->s1)
		return 0;

	mutex_lock(&devr->srq_lock);
	if (devr->s1)
		goto unlock;

	ret = mlx5_ib_dev_res_cq_init(dev);
	if (ret)
		goto unlock;

	memset(&attr, 0, sizeof(attr));
	attr.attr.max_sge = 1;
	attr.attr.max_wr = 1;
	attr.srq_type = IB_SRQT_XRC;
	attr.ext.cq = devr->c0;

	s0 = ib_create_srq(devr->p0, &attr);
	if (IS_ERR(s0)) {
		ret = PTR_ERR(s0);
		mlx5_ib_err(dev, "Couldn't create SRQ 0 for res init, err=%d\n", ret);
		goto unlock;
	}

	memset(&attr, 0, sizeof(attr));
	attr.attr.max_sge = 1;
	attr.attr.max_wr = 1;
	attr.srq_type = IB_SRQT_BASIC;

	s1 = ib_create_srq(devr->p0, &attr);
	if (IS_ERR(s1)) {
		ret = PTR_ERR(s1);
		mlx5_ib_err(dev, "Couldn't create SRQ 1 for res init, err=%d\n", ret);
		ib_destroy_srq(s0);
	}

	devr->s0 = s0;
	devr->s1 = s1;

unlock:
	mutex_unlock(&devr->srq_lock);
	return ret;
}

static int mlx5_ib_dev_res_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_ib_resources *devr = &dev->devr;
	int ret;

	if (!MLX5_CAP_GEN(dev->mdev, xrc))
		return -EOPNOTSUPP;

	ret = mlx5_cmd_xrcd_alloc(dev->mdev, &devr->xrcdn0, 0);
	if (ret)
		return ret;

	ret = mlx5_cmd_xrcd_alloc(dev->mdev, &devr->xrcdn1, 0);
	if (ret) {
		mlx5_cmd_xrcd_dealloc(dev->mdev, devr->xrcdn0, 0);
		return ret;
	}

	mutex_init(&devr->cq_lock);
	mutex_init(&devr->srq_lock);

	return 0;
}

static void mlx5_ib_dev_res_cleanup(struct mlx5_ib_dev *dev)
{
	struct mlx5_ib_resources *devr = &dev->devr;

	/* After s0/s1 init, they are not unset during the device lifetime. */
	if (devr->s1) {
		ib_destroy_srq(devr->s1);
		ib_destroy_srq(devr->s0);
	}
	mlx5_cmd_xrcd_dealloc(dev->mdev, devr->xrcdn1, 0);
	mlx5_cmd_xrcd_dealloc(dev->mdev, devr->xrcdn0, 0);
	/* After p0/c0 init, they are not unset during the device lifetime. */
	if (devr->c0) {
		ib_destroy_cq(devr->c0);
		ib_dealloc_pd(devr->p0);
	}
	mutex_destroy(&devr->cq_lock);
	mutex_destroy(&devr->srq_lock);
}

static int
mlx5_ib_create_data_direct_resources(struct mlx5_ib_dev *dev)
{
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	struct mlx5_core_dev *mdev = dev->mdev;
	void *mkc;
	u32 mkey;
	u32 pdn;
	u32 *in;
	int err;

	err = mlx5_core_alloc_pd(mdev, &pdn);
	if (err)
		return err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		err = -ENOMEM;
		goto err;
	}

	MLX5_SET(create_mkey_in, in, data_direct, 1);
	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_PA);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, rw, 1);
	MLX5_SET(mkc, mkc, rr, 1);
	MLX5_SET(mkc, mkc, a, 1);
	MLX5_SET(mkc, mkc, pd, pdn);
	MLX5_SET(mkc, mkc, length64, 1);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	err = mlx5_core_create_mkey(mdev, &mkey, in, inlen);
	kvfree(in);
	if (err)
		goto err;

	dev->ddr.mkey = mkey;
	dev->ddr.pdn = pdn;
	return 0;

err:
	mlx5_core_dealloc_pd(mdev, pdn);
	return err;
}

static void
mlx5_ib_free_data_direct_resources(struct mlx5_ib_dev *dev)
{
	mlx5_core_destroy_mkey(dev->mdev, dev->ddr.mkey);
	mlx5_core_dealloc_pd(dev->mdev, dev->ddr.pdn);
}

static u32 get_core_cap_flags(struct ib_device *ibdev,
			      struct mlx5_hca_vport_context *rep)
{
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	enum rdma_link_layer ll = mlx5_ib_port_link_layer(ibdev, 1);
	u8 l3_type_cap = MLX5_CAP_ROCE(dev->mdev, l3_type);
	u8 roce_version_cap = MLX5_CAP_ROCE(dev->mdev, roce_version);
	bool raw_support = !mlx5_core_mp_enabled(dev->mdev);
	u32 ret = 0;

	if (rep->grh_required)
		ret |= RDMA_CORE_CAP_IB_GRH_REQUIRED;

	if (dev->num_plane)
		return ret | RDMA_CORE_CAP_PROT_IB | RDMA_CORE_CAP_IB_MAD |
			RDMA_CORE_CAP_IB_CM | RDMA_CORE_CAP_IB_SA |
			RDMA_CORE_CAP_AF_IB;
	else if (ibdev->type == RDMA_DEVICE_TYPE_SMI)
		return ret | RDMA_CORE_CAP_IB_MAD | RDMA_CORE_CAP_IB_SMI;

	if (ll == IB_LINK_LAYER_INFINIBAND)
		return ret | RDMA_CORE_PORT_IBA_IB;

	if (raw_support)
		ret |= RDMA_CORE_PORT_RAW_PACKET;

	if (!(l3_type_cap & MLX5_ROCE_L3_TYPE_IPV4_CAP))
		return ret;

	if (!(l3_type_cap & MLX5_ROCE_L3_TYPE_IPV6_CAP))
		return ret;

	if (roce_version_cap & MLX5_ROCE_VERSION_1_CAP)
		ret |= RDMA_CORE_PORT_IBA_ROCE;

	if (roce_version_cap & MLX5_ROCE_VERSION_2_CAP)
		ret |= RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;

	return ret;
}

static int mlx5_port_immutable(struct ib_device *ibdev, u32 port_num,
			       struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	struct mlx5_ib_dev *dev = to_mdev(ibdev);
	enum rdma_link_layer ll = mlx5_ib_port_link_layer(ibdev, port_num);
	struct mlx5_hca_vport_context rep = {0};
	int err;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	if (ll == IB_LINK_LAYER_INFINIBAND) {
		if (ibdev->type == RDMA_DEVICE_TYPE_SMI)
			port_num = smi_to_native_portnum(dev, port_num);

		err = mlx5_query_hca_vport_context(dev->mdev, 0, port_num, 0,
						   &rep);
		if (err)
			return err;
	}

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	immutable->core_cap_flags = get_core_cap_flags(ibdev, &rep);
	immutable->max_mad_size = IB_MGMT_MAD_SIZE;

	return 0;
}

static int mlx5_port_rep_immutable(struct ib_device *ibdev, u32 port_num,
				   struct ib_port_immutable *immutable)
{
	struct ib_port_attr attr;
	int err;

	immutable->core_cap_flags = RDMA_CORE_PORT_RAW_PACKET;

	err = ib_query_port(ibdev, port_num, &attr);
	if (err)
		return err;

	immutable->pkey_tbl_len = attr.pkey_tbl_len;
	immutable->gid_tbl_len = attr.gid_tbl_len;
	immutable->core_cap_flags = RDMA_CORE_PORT_RAW_PACKET;

	return 0;
}

static void get_dev_fw_str(struct ib_device *ibdev, char *str)
{
	struct mlx5_ib_dev *dev =
		container_of(ibdev, struct mlx5_ib_dev, ib_dev);
	snprintf(str, IB_FW_VERSION_NAME_MAX, "%d.%d.%04d",
		 fw_rev_maj(dev->mdev), fw_rev_min(dev->mdev),
		 fw_rev_sub(dev->mdev));
}

static int lag_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5_ib_dev *dev = container_of(nb, struct mlx5_ib_dev,
					       lag_events);
	struct mlx5_core_dev *mdev = dev->mdev;
	struct ib_device *ibdev = &dev->ib_dev;
	struct net_device *old_ndev = NULL;
	struct mlx5_ib_port *port;
	struct net_device *ndev;
	u32 portnum = 0;
	int ret = 0;
	int i;

	switch (event) {
	case MLX5_DRIVER_EVENT_ACTIVE_BACKUP_LAG_CHANGE_LOWERSTATE:
		ndev = data;
		if (ndev) {
			if (!mlx5_lag_is_roce(mdev)) {
				// sriov lag
				for (i = 0; i < dev->num_ports; i++) {
					port = &dev->port[i];
					if (port->rep && port->rep->vport ==
					    MLX5_VPORT_UPLINK) {
						portnum = i;
						break;
					}
				}
			}
			old_ndev = ib_device_get_netdev(ibdev, portnum + 1);
			ret = ib_device_set_netdev(ibdev, ndev, portnum + 1);
			if (ret)
				goto out;

			if (old_ndev)
				roce_del_all_netdev_gids(ibdev, portnum + 1,
							 old_ndev);
			rdma_roce_rescan_port(ibdev, portnum + 1);
		}
		break;
	default:
		return NOTIFY_DONE;
	}

out:
	dev_put(old_ndev);
	return notifier_from_errno(ret);
}

static void mlx5e_lag_event_register(struct mlx5_ib_dev *dev)
{
	dev->lag_events.notifier_call = lag_event;
	blocking_notifier_chain_register(&dev->mdev->priv.lag_nh,
					 &dev->lag_events);
}

static void mlx5e_lag_event_unregister(struct mlx5_ib_dev *dev)
{
	blocking_notifier_chain_unregister(&dev->mdev->priv.lag_nh,
					   &dev->lag_events);
}

static int mlx5_eth_lag_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_flow_namespace *ns = mlx5_get_flow_namespace(mdev,
								 MLX5_FLOW_NAMESPACE_LAG);
	struct mlx5_flow_table *ft;
	int err;

	if (!ns || !mlx5_lag_is_active(mdev))
		return 0;

	err = mlx5_cmd_create_vport_lag(mdev);
	if (err)
		return err;

	ft = mlx5_create_lag_demux_flow_table(ns, 0, 0);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		goto err_destroy_vport_lag;
	}

	mlx5e_lag_event_register(dev);
	dev->flow_db->lag_demux_ft = ft;
	dev->lag_ports = mlx5_lag_get_num_ports(mdev);
	dev->lag_active = true;
	return 0;

err_destroy_vport_lag:
	mlx5_cmd_destroy_vport_lag(mdev);
	return err;
}

static void mlx5_eth_lag_cleanup(struct mlx5_ib_dev *dev)
{
	struct mlx5_core_dev *mdev = dev->mdev;

	if (dev->lag_active) {
		dev->lag_active = false;

		mlx5e_lag_event_unregister(dev);
		mlx5_destroy_flow_table(dev->flow_db->lag_demux_ft);
		dev->flow_db->lag_demux_ft = NULL;

		mlx5_cmd_destroy_vport_lag(mdev);
	}
}

static void mlx5_netdev_notifier_register(struct mlx5_roce *roce,
					  struct net_device *netdev)
{
	int err;

	if (roce->tracking_netdev)
		return;
	roce->tracking_netdev = netdev;
	roce->nb.notifier_call = mlx5_netdev_event;
	err = register_netdevice_notifier_dev_net(netdev, &roce->nb, &roce->nn);
	WARN_ON(err);
}

static void mlx5_netdev_notifier_unregister(struct mlx5_roce *roce)
{
	if (!roce->tracking_netdev)
		return;
	unregister_netdevice_notifier_dev_net(roce->tracking_netdev, &roce->nb,
					      &roce->nn);
	roce->tracking_netdev = NULL;
}

static int mlx5e_mdev_notifier_event(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct mlx5_roce *roce = container_of(nb, struct mlx5_roce, mdev_nb);
	struct net_device *netdev = data;

	switch (event) {
	case MLX5_DRIVER_EVENT_UPLINK_NETDEV:
		if (netdev)
			mlx5_netdev_notifier_register(roce, netdev);
		else
			mlx5_netdev_notifier_unregister(roce);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static void mlx5_mdev_netdev_track(struct mlx5_ib_dev *dev, u32 port_num)
{
	struct mlx5_roce *roce = &dev->port[port_num].roce;

	roce->mdev_nb.notifier_call = mlx5e_mdev_notifier_event;
	mlx5_blocking_notifier_register(dev->mdev, &roce->mdev_nb);
	mlx5_core_uplink_netdev_event_replay(dev->mdev);
}

static void mlx5_mdev_netdev_untrack(struct mlx5_ib_dev *dev, u32 port_num)
{
	struct mlx5_roce *roce = &dev->port[port_num].roce;

	mlx5_blocking_notifier_unregister(dev->mdev, &roce->mdev_nb);
	mlx5_netdev_notifier_unregister(roce);
}

static int mlx5_enable_eth(struct mlx5_ib_dev *dev)
{
	int err;

	if (!dev->is_rep && dev->profile != &raw_eth_profile) {
		err = mlx5_nic_vport_enable_roce(dev->mdev);
		if (err)
			return err;
	}

	err = mlx5_eth_lag_init(dev);
	if (err)
		goto err_disable_roce;

	return 0;

err_disable_roce:
	if (!dev->is_rep && dev->profile != &raw_eth_profile)
		mlx5_nic_vport_disable_roce(dev->mdev);

	return err;
}

static void mlx5_disable_eth(struct mlx5_ib_dev *dev)
{
	mlx5_eth_lag_cleanup(dev);
	if (!dev->is_rep && dev->profile != &raw_eth_profile)
		mlx5_nic_vport_disable_roce(dev->mdev);
}

static int mlx5_ib_rn_get_params(struct ib_device *device, u32 port_num,
				 enum rdma_netdev_t type,
				 struct rdma_netdev_alloc_params *params)
{
	if (type != RDMA_NETDEV_IPOIB)
		return -EOPNOTSUPP;

	return mlx5_rdma_rn_get_params(to_mdev(device)->mdev, device, params);
}

static ssize_t delay_drop_timeout_read(struct file *filp, char __user *buf,
				       size_t count, loff_t *pos)
{
	struct mlx5_ib_delay_drop *delay_drop = filp->private_data;
	char lbuf[20];
	int len;

	len = snprintf(lbuf, sizeof(lbuf), "%u\n", delay_drop->timeout);
	return simple_read_from_buffer(buf, count, pos, lbuf, len);
}

static ssize_t delay_drop_timeout_write(struct file *filp, const char __user *buf,
					size_t count, loff_t *pos)
{
	struct mlx5_ib_delay_drop *delay_drop = filp->private_data;
	u32 timeout;
	u32 var;

	if (kstrtouint_from_user(buf, count, 0, &var))
		return -EFAULT;

	timeout = min_t(u32, roundup(var, 100), MLX5_MAX_DELAY_DROP_TIMEOUT_MS *
			1000);
	if (timeout != var)
		mlx5_ib_dbg(delay_drop->dev, "Round delay drop timeout to %u usec\n",
			    timeout);

	delay_drop->timeout = timeout;

	return count;
}

static const struct file_operations fops_delay_drop_timeout = {
	.owner	= THIS_MODULE,
	.open	= simple_open,
	.write	= delay_drop_timeout_write,
	.read	= delay_drop_timeout_read,
};

static void mlx5_ib_unbind_slave_port(struct mlx5_ib_dev *ibdev,
				      struct mlx5_ib_multiport_info *mpi)
{
	u32 port_num = mlx5_core_native_port_num(mpi->mdev) - 1;
	struct mlx5_ib_port *port = &ibdev->port[port_num];
	int comps;
	int err;
	int i;

	lockdep_assert_held(&mlx5_ib_multiport_mutex);

	mlx5_core_mp_event_replay(ibdev->mdev,
				  MLX5_DRIVER_EVENT_AFFILIATION_REMOVED,
				  NULL);
	mlx5_core_mp_event_replay(mpi->mdev,
				  MLX5_DRIVER_EVENT_AFFILIATION_REMOVED,
				  NULL);

	mlx5_ib_cleanup_cong_debugfs(ibdev, port_num);

	spin_lock(&port->mp.mpi_lock);
	if (!mpi->ibdev) {
		spin_unlock(&port->mp.mpi_lock);
		return;
	}

	mpi->ibdev = NULL;

	spin_unlock(&port->mp.mpi_lock);
	if (mpi->mdev_events.notifier_call)
		mlx5_notifier_unregister(mpi->mdev, &mpi->mdev_events);
	mpi->mdev_events.notifier_call = NULL;
	mlx5_mdev_netdev_untrack(ibdev, port_num);
	spin_lock(&port->mp.mpi_lock);

	comps = mpi->mdev_refcnt;
	if (comps) {
		mpi->unaffiliate = true;
		init_completion(&mpi->unref_comp);
		spin_unlock(&port->mp.mpi_lock);

		for (i = 0; i < comps; i++)
			wait_for_completion(&mpi->unref_comp);

		spin_lock(&port->mp.mpi_lock);
		mpi->unaffiliate = false;
	}

	port->mp.mpi = NULL;

	spin_unlock(&port->mp.mpi_lock);

	err = mlx5_nic_vport_unaffiliate_multiport(mpi->mdev);

	mlx5_ib_dbg(ibdev, "unaffiliated port %u\n", port_num + 1);
	/* Log an error, still needed to cleanup the pointers and add
	 * it back to the list.
	 */
	if (err)
		mlx5_ib_err(ibdev, "Failed to unaffiliate port %u\n",
			    port_num + 1);

	ibdev->port[port_num].roce.last_port_state = IB_PORT_DOWN;
}

static bool mlx5_ib_bind_slave_port(struct mlx5_ib_dev *ibdev,
				    struct mlx5_ib_multiport_info *mpi)
{
	u32 port_num = mlx5_core_native_port_num(mpi->mdev) - 1;
	u64 key;
	int err;

	lockdep_assert_held(&mlx5_ib_multiport_mutex);

	spin_lock(&ibdev->port[port_num].mp.mpi_lock);
	if (ibdev->port[port_num].mp.mpi) {
		mlx5_ib_dbg(ibdev, "port %u already affiliated.\n",
			    port_num + 1);
		spin_unlock(&ibdev->port[port_num].mp.mpi_lock);
		return false;
	}

	ibdev->port[port_num].mp.mpi = mpi;
	mpi->ibdev = ibdev;
	mpi->mdev_events.notifier_call = NULL;
	spin_unlock(&ibdev->port[port_num].mp.mpi_lock);

	err = mlx5_nic_vport_affiliate_multiport(ibdev->mdev, mpi->mdev);
	if (err)
		goto unbind;

	mlx5_mdev_netdev_track(ibdev, port_num);

	mpi->mdev_events.notifier_call = mlx5_ib_event_slave_port;
	mlx5_notifier_register(mpi->mdev, &mpi->mdev_events);

	mlx5_ib_init_cong_debugfs(ibdev, port_num);

	key = mpi->mdev->priv.adev_idx;
	mlx5_core_mp_event_replay(mpi->mdev,
				  MLX5_DRIVER_EVENT_AFFILIATION_DONE,
				  &key);
	mlx5_core_mp_event_replay(ibdev->mdev,
				  MLX5_DRIVER_EVENT_AFFILIATION_DONE,
				  &key);

	return true;

unbind:
	mlx5_ib_unbind_slave_port(ibdev, mpi);
	return false;
}

static int mlx5_ib_data_direct_init(struct mlx5_ib_dev *dev)
{
	char vuid[MLX5_ST_SZ_BYTES(array1024_auto) + 1] = {};
	int ret;

	if (!MLX5_CAP_GEN(dev->mdev, data_direct) ||
	    !MLX5_CAP_GEN_2(dev->mdev, query_vuid))
		return 0;

	ret = mlx5_cmd_query_vuid(dev->mdev, true, vuid);
	if (ret)
		return ret;

	ret = mlx5_ib_create_data_direct_resources(dev);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&dev->data_direct_mr_list);
	ret = mlx5_data_direct_ib_reg(dev, vuid);
	if (ret)
		mlx5_ib_free_data_direct_resources(dev);

	return ret;
}

static void mlx5_ib_data_direct_cleanup(struct mlx5_ib_dev *dev)
{
	if (!MLX5_CAP_GEN(dev->mdev, data_direct) ||
	    !MLX5_CAP_GEN_2(dev->mdev, query_vuid))
		return;

	mlx5_data_direct_ib_unreg(dev);
	mlx5_ib_free_data_direct_resources(dev);
}

static int mlx5_ib_init_multiport_master(struct mlx5_ib_dev *dev)
{
	u32 port_num = mlx5_core_native_port_num(dev->mdev) - 1;
	enum rdma_link_layer ll = mlx5_ib_port_link_layer(&dev->ib_dev,
							  port_num + 1);
	struct mlx5_ib_multiport_info *mpi;
	int err;
	u32 i;

	if (!mlx5_core_is_mp_master(dev->mdev) || ll != IB_LINK_LAYER_ETHERNET)
		return 0;

	err = mlx5_query_nic_vport_system_image_guid(dev->mdev,
						     &dev->sys_image_guid);
	if (err)
		return err;

	err = mlx5_nic_vport_enable_roce(dev->mdev);
	if (err)
		return err;

	mutex_lock(&mlx5_ib_multiport_mutex);
	for (i = 0; i < dev->num_ports; i++) {
		bool bound = false;

		/* build a stub multiport info struct for the native port. */
		if (i == port_num) {
			mpi = kzalloc(sizeof(*mpi), GFP_KERNEL);
			if (!mpi) {
				mutex_unlock(&mlx5_ib_multiport_mutex);
				mlx5_nic_vport_disable_roce(dev->mdev);
				return -ENOMEM;
			}

			mpi->is_master = true;
			mpi->mdev = dev->mdev;
			mpi->sys_image_guid = dev->sys_image_guid;
			dev->port[i].mp.mpi = mpi;
			mpi->ibdev = dev;
			mpi = NULL;
			continue;
		}

		list_for_each_entry(mpi, &mlx5_ib_unaffiliated_port_list,
				    list) {
			if (dev->sys_image_guid == mpi->sys_image_guid &&
			    (mlx5_core_native_port_num(mpi->mdev) - 1) == i &&
			    mlx5_core_same_coredev_type(dev->mdev, mpi->mdev)) {
				bound = mlx5_ib_bind_slave_port(dev, mpi);
			}

			if (bound) {
				dev_dbg(mpi->mdev->device,
					"removing port from unaffiliated list.\n");
				mlx5_ib_dbg(dev, "port %d bound\n", i + 1);
				list_del(&mpi->list);
				break;
			}
		}
		if (!bound)
			mlx5_ib_dbg(dev, "no free port found for port %d\n",
				    i + 1);
	}

	list_add_tail(&dev->ib_dev_list, &mlx5_ib_dev_list);
	mutex_unlock(&mlx5_ib_multiport_mutex);
	return err;
}

static void mlx5_ib_cleanup_multiport_master(struct mlx5_ib_dev *dev)
{
	u32 port_num = mlx5_core_native_port_num(dev->mdev) - 1;
	enum rdma_link_layer ll = mlx5_ib_port_link_layer(&dev->ib_dev,
							  port_num + 1);
	u32 i;

	if (!mlx5_core_is_mp_master(dev->mdev) || ll != IB_LINK_LAYER_ETHERNET)
		return;

	mutex_lock(&mlx5_ib_multiport_mutex);
	for (i = 0; i < dev->num_ports; i++) {
		if (dev->port[i].mp.mpi) {
			/* Destroy the native port stub */
			if (i == port_num) {
				kfree(dev->port[i].mp.mpi);
				dev->port[i].mp.mpi = NULL;
			} else {
				mlx5_ib_dbg(dev, "unbinding port_num: %u\n",
					    i + 1);
				list_add_tail(&dev->port[i].mp.mpi->list,
					      &mlx5_ib_unaffiliated_port_list);
				mlx5_ib_unbind_slave_port(dev,
							  dev->port[i].mp.mpi);
			}
		}
	}

	mlx5_ib_dbg(dev, "removing from devlist\n");
	list_del(&dev->ib_dev_list);
	mutex_unlock(&mlx5_ib_multiport_mutex);

	mlx5_nic_vport_disable_roce(dev->mdev);
}

static int mmap_obj_cleanup(struct ib_uobject *uobject,
			    enum rdma_remove_reason why,
			    struct uverbs_attr_bundle *attrs)
{
	struct mlx5_user_mmap_entry *obj = uobject->object;

	rdma_user_mmap_entry_remove(&obj->rdma_entry);
	return 0;
}

static int mlx5_rdma_user_mmap_entry_insert(struct mlx5_ib_ucontext *c,
					    struct mlx5_user_mmap_entry *entry,
					    size_t length)
{
	return rdma_user_mmap_entry_insert_range(
		&c->ibucontext, &entry->rdma_entry, length,
		(MLX5_IB_MMAP_OFFSET_START << 16),
		((MLX5_IB_MMAP_OFFSET_END << 16) + (1UL << 16) - 1));
}

static struct mlx5_user_mmap_entry *
alloc_var_entry(struct mlx5_ib_ucontext *c)
{
	struct mlx5_user_mmap_entry *entry;
	struct mlx5_var_table *var_table;
	u32 page_idx;
	int err;

	var_table = &to_mdev(c->ibucontext.device)->var_table;
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&var_table->bitmap_lock);
	page_idx = find_first_zero_bit(var_table->bitmap,
				       var_table->num_var_hw_entries);
	if (page_idx >= var_table->num_var_hw_entries) {
		err = -ENOSPC;
		mutex_unlock(&var_table->bitmap_lock);
		goto end;
	}

	set_bit(page_idx, var_table->bitmap);
	mutex_unlock(&var_table->bitmap_lock);

	entry->address = var_table->hw_start_addr +
				(page_idx * var_table->stride_size);
	entry->page_idx = page_idx;
	entry->mmap_flag = MLX5_IB_MMAP_TYPE_VAR;

	err = mlx5_rdma_user_mmap_entry_insert(c, entry,
					       var_table->stride_size);
	if (err)
		goto err_insert;

	return entry;

err_insert:
	mutex_lock(&var_table->bitmap_lock);
	clear_bit(page_idx, var_table->bitmap);
	mutex_unlock(&var_table->bitmap_lock);
end:
	kfree(entry);
	return ERR_PTR(err);
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_VAR_OBJ_ALLOC)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_VAR_OBJ_ALLOC_HANDLE);
	struct mlx5_ib_ucontext *c;
	struct mlx5_user_mmap_entry *entry;
	u64 mmap_offset;
	u32 length;
	int err;

	c = to_mucontext(ib_uverbs_get_ucontext(attrs));
	if (IS_ERR(c))
		return PTR_ERR(c);

	entry = alloc_var_entry(c);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	mmap_offset = mlx5_entry_to_mmap_offset(entry);
	length = entry->rdma_entry.npages * PAGE_SIZE;
	uobj->object = entry;
	uverbs_finalize_uobj_create(attrs, MLX5_IB_ATTR_VAR_OBJ_ALLOC_HANDLE);

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_VAR_OBJ_ALLOC_MMAP_OFFSET,
			     &mmap_offset, sizeof(mmap_offset));
	if (err)
		return err;

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_VAR_OBJ_ALLOC_PAGE_ID,
			     &entry->page_idx, sizeof(entry->page_idx));
	if (err)
		return err;

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_VAR_OBJ_ALLOC_MMAP_LENGTH,
			     &length, sizeof(length));
	return err;
}

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_VAR_OBJ_ALLOC,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_VAR_OBJ_ALLOC_HANDLE,
			MLX5_IB_OBJECT_VAR,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_VAR_OBJ_ALLOC_PAGE_ID,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_VAR_OBJ_ALLOC_MMAP_LENGTH,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_VAR_OBJ_ALLOC_MMAP_OFFSET,
			    UVERBS_ATTR_TYPE(u64),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	MLX5_IB_METHOD_VAR_OBJ_DESTROY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_VAR_OBJ_DESTROY_HANDLE,
			MLX5_IB_OBJECT_VAR,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(MLX5_IB_OBJECT_VAR,
			    UVERBS_TYPE_ALLOC_IDR(mmap_obj_cleanup),
			    &UVERBS_METHOD(MLX5_IB_METHOD_VAR_OBJ_ALLOC),
			    &UVERBS_METHOD(MLX5_IB_METHOD_VAR_OBJ_DESTROY));

static bool var_is_supported(struct ib_device *device)
{
	struct mlx5_ib_dev *dev = to_mdev(device);

	return (MLX5_CAP_GEN_64(dev->mdev, general_obj_types) &
			MLX5_GENERAL_OBJ_TYPES_CAP_VIRTIO_NET_Q);
}

static struct mlx5_user_mmap_entry *
alloc_uar_entry(struct mlx5_ib_ucontext *c,
		enum mlx5_ib_uapi_uar_alloc_type alloc_type)
{
	struct mlx5_user_mmap_entry *entry;
	struct mlx5_ib_dev *dev;
	u32 uar_index;
	int err;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return ERR_PTR(-ENOMEM);

	dev = to_mdev(c->ibucontext.device);
	err = mlx5_cmd_uar_alloc(dev->mdev, &uar_index, c->devx_uid);
	if (err)
		goto end;

	entry->page_idx = uar_index;
	entry->address = uar_index2paddress(dev, uar_index);
	if (alloc_type == MLX5_IB_UAPI_UAR_ALLOC_TYPE_BF)
		entry->mmap_flag = MLX5_IB_MMAP_TYPE_UAR_WC;
	else
		entry->mmap_flag = MLX5_IB_MMAP_TYPE_UAR_NC;

	err = mlx5_rdma_user_mmap_entry_insert(c, entry, PAGE_SIZE);
	if (err)
		goto err_insert;

	return entry;

err_insert:
	mlx5_cmd_uar_dealloc(dev->mdev, uar_index, c->devx_uid);
end:
	kfree(entry);
	return ERR_PTR(err);
}

static int UVERBS_HANDLER(MLX5_IB_METHOD_UAR_OBJ_ALLOC)(
	struct uverbs_attr_bundle *attrs)
{
	struct ib_uobject *uobj = uverbs_attr_get_uobject(
		attrs, MLX5_IB_ATTR_UAR_OBJ_ALLOC_HANDLE);
	enum mlx5_ib_uapi_uar_alloc_type alloc_type;
	struct mlx5_ib_ucontext *c;
	struct mlx5_user_mmap_entry *entry;
	u64 mmap_offset;
	u32 length;
	int err;

	c = to_mucontext(ib_uverbs_get_ucontext(attrs));
	if (IS_ERR(c))
		return PTR_ERR(c);

	err = uverbs_get_const(&alloc_type, attrs,
			       MLX5_IB_ATTR_UAR_OBJ_ALLOC_TYPE);
	if (err)
		return err;

	if (alloc_type != MLX5_IB_UAPI_UAR_ALLOC_TYPE_BF &&
	    alloc_type != MLX5_IB_UAPI_UAR_ALLOC_TYPE_NC)
		return -EOPNOTSUPP;

	if (!mlx5_wc_support_get(to_mdev(c->ibucontext.device)->mdev) &&
	    alloc_type == MLX5_IB_UAPI_UAR_ALLOC_TYPE_BF)
		return -EOPNOTSUPP;

	entry = alloc_uar_entry(c, alloc_type);
	if (IS_ERR(entry))
		return PTR_ERR(entry);

	mmap_offset = mlx5_entry_to_mmap_offset(entry);
	length = entry->rdma_entry.npages * PAGE_SIZE;
	uobj->object = entry;
	uverbs_finalize_uobj_create(attrs, MLX5_IB_ATTR_UAR_OBJ_ALLOC_HANDLE);

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_UAR_OBJ_ALLOC_MMAP_OFFSET,
			     &mmap_offset, sizeof(mmap_offset));
	if (err)
		return err;

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_UAR_OBJ_ALLOC_PAGE_ID,
			     &entry->page_idx, sizeof(entry->page_idx));
	if (err)
		return err;

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_UAR_OBJ_ALLOC_MMAP_LENGTH,
			     &length, sizeof(length));
	return err;
}

DECLARE_UVERBS_NAMED_METHOD(
	MLX5_IB_METHOD_UAR_OBJ_ALLOC,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_UAR_OBJ_ALLOC_HANDLE,
			MLX5_IB_OBJECT_UAR,
			UVERBS_ACCESS_NEW,
			UA_MANDATORY),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_UAR_OBJ_ALLOC_TYPE,
			     enum mlx5_ib_uapi_uar_alloc_type,
			     UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_UAR_OBJ_ALLOC_PAGE_ID,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_UAR_OBJ_ALLOC_MMAP_LENGTH,
			   UVERBS_ATTR_TYPE(u32),
			   UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_UAR_OBJ_ALLOC_MMAP_OFFSET,
			    UVERBS_ATTR_TYPE(u64),
			    UA_MANDATORY));

DECLARE_UVERBS_NAMED_METHOD_DESTROY(
	MLX5_IB_METHOD_UAR_OBJ_DESTROY,
	UVERBS_ATTR_IDR(MLX5_IB_ATTR_UAR_OBJ_DESTROY_HANDLE,
			MLX5_IB_OBJECT_UAR,
			UVERBS_ACCESS_DESTROY,
			UA_MANDATORY));

DECLARE_UVERBS_NAMED_OBJECT(MLX5_IB_OBJECT_UAR,
			    UVERBS_TYPE_ALLOC_IDR(mmap_obj_cleanup),
			    &UVERBS_METHOD(MLX5_IB_METHOD_UAR_OBJ_ALLOC),
			    &UVERBS_METHOD(MLX5_IB_METHOD_UAR_OBJ_DESTROY));

ADD_UVERBS_ATTRIBUTES_SIMPLE(
	mlx5_ib_query_context,
	UVERBS_OBJECT_DEVICE,
	UVERBS_METHOD_QUERY_CONTEXT,
	UVERBS_ATTR_PTR_OUT(
		MLX5_IB_ATTR_QUERY_CONTEXT_RESP_UCTX,
		UVERBS_ATTR_STRUCT(struct mlx5_ib_alloc_ucontext_resp,
				   dump_fill_mkey),
		UA_MANDATORY));

ADD_UVERBS_ATTRIBUTES_SIMPLE(
	mlx5_ib_reg_dmabuf_mr,
	UVERBS_OBJECT_MR,
	UVERBS_METHOD_REG_DMABUF_MR,
	UVERBS_ATTR_FLAGS_IN(MLX5_IB_ATTR_REG_DMABUF_MR_ACCESS_FLAGS,
			     enum mlx5_ib_uapi_reg_dmabuf_flags,
			     UA_OPTIONAL));

static const struct uapi_definition mlx5_ib_defs[] = {
	UAPI_DEF_CHAIN(mlx5_ib_devx_defs),
	UAPI_DEF_CHAIN(mlx5_ib_flow_defs),
	UAPI_DEF_CHAIN(mlx5_ib_qos_defs),
	UAPI_DEF_CHAIN(mlx5_ib_std_types_defs),
	UAPI_DEF_CHAIN(mlx5_ib_dm_defs),
	UAPI_DEF_CHAIN(mlx5_ib_create_cq_defs),

	UAPI_DEF_CHAIN_OBJ_TREE(UVERBS_OBJECT_DEVICE, &mlx5_ib_query_context),
	UAPI_DEF_CHAIN_OBJ_TREE(UVERBS_OBJECT_MR, &mlx5_ib_reg_dmabuf_mr),
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(MLX5_IB_OBJECT_VAR,
				UAPI_DEF_IS_OBJ_SUPPORTED(var_is_supported)),
	UAPI_DEF_CHAIN_OBJ_TREE_NAMED(MLX5_IB_OBJECT_UAR),
	{}
};

static void mlx5_ib_stage_init_cleanup(struct mlx5_ib_dev *dev)
{
	mlx5_ib_data_direct_cleanup(dev);
	mlx5_ib_cleanup_multiport_master(dev);
	WARN_ON(!xa_empty(&dev->odp_mkeys));
	mutex_destroy(&dev->cap_mask_mutex);
	WARN_ON(!xa_empty(&dev->sig_mrs));
	WARN_ON(!bitmap_empty(dev->dm.memic_alloc_pages, MLX5_MAX_MEMIC_PAGES));
	mlx5r_macsec_dealloc_gids(dev);
}

static int mlx5_ib_stage_init_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_core_dev *mdev = dev->mdev;
	int err, i;

	dev->ib_dev.node_type = RDMA_NODE_IB_CA;
	dev->ib_dev.local_dma_lkey = 0 /* not supported for now */;
	dev->ib_dev.dev.parent = mdev->device;
	dev->ib_dev.lag_flags = RDMA_LAG_FLAGS_HASH_ALL_SLAVES;

	for (i = 0; i < dev->num_ports; i++) {
		spin_lock_init(&dev->port[i].mp.mpi_lock);
		dev->port[i].roce.dev = dev;
		dev->port[i].roce.native_port_num = i + 1;
		dev->port[i].roce.last_port_state = IB_PORT_DOWN;
	}

	err = mlx5r_cmd_query_special_mkeys(dev);
	if (err)
		return err;

	err = mlx5r_macsec_init_gids_and_devlist(dev);
	if (err)
		return err;

	err = mlx5_ib_init_multiport_master(dev);
	if (err)
		goto err;

	err = set_has_smi_cap(dev);
	if (err)
		goto err_mp;

	err = mlx5_query_max_pkeys(&dev->ib_dev, &dev->pkey_table_len);
	if (err)
		goto err_mp;

	if (mlx5_use_mad_ifc(dev))
		get_ext_port_caps(dev);

	dev->ib_dev.num_comp_vectors    = mlx5_comp_vectors_max(mdev);

	mutex_init(&dev->cap_mask_mutex);
	mutex_init(&dev->data_direct_lock);
	INIT_LIST_HEAD(&dev->qp_list);
	spin_lock_init(&dev->reset_flow_resource_lock);
	xa_init(&dev->odp_mkeys);
	xa_init(&dev->sig_mrs);
	atomic_set(&dev->mkey_var, 0);

	spin_lock_init(&dev->dm.lock);
	dev->dm.dev = mdev;
	err = mlx5_ib_data_direct_init(dev);
	if (err)
		goto err_mp;

	return 0;
err_mp:
	mlx5_ib_cleanup_multiport_master(dev);
err:
	mlx5r_macsec_dealloc_gids(dev);
	return err;
}

static struct ib_device *mlx5_ib_add_sub_dev(struct ib_device *parent,
					     enum rdma_nl_dev_type type,
					     const char *name);
static void mlx5_ib_del_sub_dev(struct ib_device *sub_dev);

static const struct ib_device_ops mlx5_ib_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_MLX5,
	.uverbs_abi_ver	= MLX5_IB_UVERBS_ABI_VERSION,

	.add_gid = mlx5_ib_add_gid,
	.add_sub_dev = mlx5_ib_add_sub_dev,
	.alloc_mr = mlx5_ib_alloc_mr,
	.alloc_mr_integrity = mlx5_ib_alloc_mr_integrity,
	.alloc_pd = mlx5_ib_alloc_pd,
	.alloc_ucontext = mlx5_ib_alloc_ucontext,
	.attach_mcast = mlx5_ib_mcg_attach,
	.check_mr_status = mlx5_ib_check_mr_status,
	.create_ah = mlx5_ib_create_ah,
	.create_cq = mlx5_ib_create_cq,
	.create_qp = mlx5_ib_create_qp,
	.create_srq = mlx5_ib_create_srq,
	.create_user_ah = mlx5_ib_create_ah,
	.dealloc_pd = mlx5_ib_dealloc_pd,
	.dealloc_ucontext = mlx5_ib_dealloc_ucontext,
	.del_gid = mlx5_ib_del_gid,
	.del_sub_dev = mlx5_ib_del_sub_dev,
	.dereg_mr = mlx5_ib_dereg_mr,
	.destroy_ah = mlx5_ib_destroy_ah,
	.destroy_cq = mlx5_ib_destroy_cq,
	.destroy_qp = mlx5_ib_destroy_qp,
	.destroy_srq = mlx5_ib_destroy_srq,
	.detach_mcast = mlx5_ib_mcg_detach,
	.disassociate_ucontext = mlx5_ib_disassociate_ucontext,
	.drain_rq = mlx5_ib_drain_rq,
	.drain_sq = mlx5_ib_drain_sq,
	.device_group = &mlx5_attr_group,
	.get_dev_fw_str = get_dev_fw_str,
	.get_dma_mr = mlx5_ib_get_dma_mr,
	.get_link_layer = mlx5_ib_port_link_layer,
	.map_mr_sg = mlx5_ib_map_mr_sg,
	.map_mr_sg_pi = mlx5_ib_map_mr_sg_pi,
	.mmap = mlx5_ib_mmap,
	.mmap_free = mlx5_ib_mmap_free,
	.modify_cq = mlx5_ib_modify_cq,
	.modify_device = mlx5_ib_modify_device,
	.modify_port = mlx5_ib_modify_port,
	.modify_qp = mlx5_ib_modify_qp,
	.modify_srq = mlx5_ib_modify_srq,
	.poll_cq = mlx5_ib_poll_cq,
	.post_recv = mlx5_ib_post_recv_nodrain,
	.post_send = mlx5_ib_post_send_nodrain,
	.post_srq_recv = mlx5_ib_post_srq_recv,
	.process_mad = mlx5_ib_process_mad,
	.query_ah = mlx5_ib_query_ah,
	.query_device = mlx5_ib_query_device,
	.query_gid = mlx5_ib_query_gid,
	.query_pkey = mlx5_ib_query_pkey,
	.query_qp = mlx5_ib_query_qp,
	.query_srq = mlx5_ib_query_srq,
	.query_ucontext = mlx5_ib_query_ucontext,
	.reg_user_mr = mlx5_ib_reg_user_mr,
	.reg_user_mr_dmabuf = mlx5_ib_reg_user_mr_dmabuf,
	.req_notify_cq = mlx5_ib_arm_cq,
	.rereg_user_mr = mlx5_ib_rereg_user_mr,
	.resize_cq = mlx5_ib_resize_cq,
	.ufile_hw_cleanup = mlx5_ib_ufile_hw_cleanup,

	INIT_RDMA_OBJ_SIZE(ib_ah, mlx5_ib_ah, ibah),
	INIT_RDMA_OBJ_SIZE(ib_counters, mlx5_ib_mcounters, ibcntrs),
	INIT_RDMA_OBJ_SIZE(ib_cq, mlx5_ib_cq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_pd, mlx5_ib_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_qp, mlx5_ib_qp, ibqp),
	INIT_RDMA_OBJ_SIZE(ib_srq, mlx5_ib_srq, ibsrq),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, mlx5_ib_ucontext, ibucontext),
};

static const struct ib_device_ops mlx5_ib_dev_ipoib_enhanced_ops = {
	.rdma_netdev_get_params = mlx5_ib_rn_get_params,
};

static const struct ib_device_ops mlx5_ib_dev_sriov_ops = {
	.get_vf_config = mlx5_ib_get_vf_config,
	.get_vf_guid = mlx5_ib_get_vf_guid,
	.get_vf_stats = mlx5_ib_get_vf_stats,
	.set_vf_guid = mlx5_ib_set_vf_guid,
	.set_vf_link_state = mlx5_ib_set_vf_link_state,
};

static const struct ib_device_ops mlx5_ib_dev_mw_ops = {
	.alloc_mw = mlx5_ib_alloc_mw,
	.dealloc_mw = mlx5_ib_dealloc_mw,

	INIT_RDMA_OBJ_SIZE(ib_mw, mlx5_ib_mw, ibmw),
};

static const struct ib_device_ops mlx5_ib_dev_xrc_ops = {
	.alloc_xrcd = mlx5_ib_alloc_xrcd,
	.dealloc_xrcd = mlx5_ib_dealloc_xrcd,

	INIT_RDMA_OBJ_SIZE(ib_xrcd, mlx5_ib_xrcd, ibxrcd),
};

static int mlx5_ib_init_var_table(struct mlx5_ib_dev *dev)
{
	struct mlx5_core_dev *mdev = dev->mdev;
	struct mlx5_var_table *var_table = &dev->var_table;
	u8 log_doorbell_bar_size;
	u8 log_doorbell_stride;
	u64 bar_size;

	log_doorbell_bar_size = MLX5_CAP_DEV_VDPA_EMULATION(mdev,
					log_doorbell_bar_size);
	log_doorbell_stride = MLX5_CAP_DEV_VDPA_EMULATION(mdev,
					log_doorbell_stride);
	var_table->hw_start_addr = dev->mdev->bar_addr +
				MLX5_CAP64_DEV_VDPA_EMULATION(mdev,
					doorbell_bar_offset);
	bar_size = (1ULL << log_doorbell_bar_size) * 4096;
	var_table->stride_size = 1ULL << log_doorbell_stride;
	var_table->num_var_hw_entries = div_u64(bar_size,
						var_table->stride_size);
	mutex_init(&var_table->bitmap_lock);
	var_table->bitmap = bitmap_zalloc(var_table->num_var_hw_entries,
					  GFP_KERNEL);
	return (var_table->bitmap) ? 0 : -ENOMEM;
}

static void mlx5_ib_stage_caps_cleanup(struct mlx5_ib_dev *dev)
{
	bitmap_free(dev->var_table.bitmap);
}

static int mlx5_ib_stage_caps_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_core_dev *mdev = dev->mdev;
	int err;

	if (MLX5_CAP_GEN(mdev, ipoib_enhanced_offloads) &&
	    IS_ENABLED(CONFIG_MLX5_CORE_IPOIB))
		ib_set_device_ops(&dev->ib_dev,
				  &mlx5_ib_dev_ipoib_enhanced_ops);

	if (mlx5_core_is_pf(mdev))
		ib_set_device_ops(&dev->ib_dev, &mlx5_ib_dev_sriov_ops);

	dev->umr_fence = mlx5_get_umr_fence(MLX5_CAP_GEN(mdev, umr_fence));

	if (MLX5_CAP_GEN(mdev, imaicl))
		ib_set_device_ops(&dev->ib_dev, &mlx5_ib_dev_mw_ops);

	if (MLX5_CAP_GEN(mdev, xrc))
		ib_set_device_ops(&dev->ib_dev, &mlx5_ib_dev_xrc_ops);

	if (MLX5_CAP_DEV_MEM(mdev, memic) ||
	    MLX5_CAP_GEN_64(dev->mdev, general_obj_types) &
	    MLX5_GENERAL_OBJ_TYPES_CAP_SW_ICM)
		ib_set_device_ops(&dev->ib_dev, &mlx5_ib_dev_dm_ops);

	ib_set_device_ops(&dev->ib_dev, &mlx5_ib_dev_ops);

	if (IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS))
		dev->ib_dev.driver_def = mlx5_ib_defs;

	err = init_node_data(dev);
	if (err)
		return err;

	if ((MLX5_CAP_GEN(dev->mdev, port_type) == MLX5_CAP_PORT_TYPE_ETH) &&
	    (MLX5_CAP_GEN(dev->mdev, disable_local_lb_uc) ||
	     MLX5_CAP_GEN(dev->mdev, disable_local_lb_mc)))
		mutex_init(&dev->lb.mutex);

	if (MLX5_CAP_GEN_64(dev->mdev, general_obj_types) &
			MLX5_GENERAL_OBJ_TYPES_CAP_VIRTIO_NET_Q) {
		err = mlx5_ib_init_var_table(dev);
		if (err)
			return err;
	}

	dev->ib_dev.use_cq_dim = true;

	return 0;
}

static const struct ib_device_ops mlx5_ib_dev_port_ops = {
	.get_port_immutable = mlx5_port_immutable,
	.query_port = mlx5_ib_query_port,
};

static int mlx5_ib_stage_non_default_cb(struct mlx5_ib_dev *dev)
{
	ib_set_device_ops(&dev->ib_dev, &mlx5_ib_dev_port_ops);
	return 0;
}

static const struct ib_device_ops mlx5_ib_dev_port_rep_ops = {
	.get_port_immutable = mlx5_port_rep_immutable,
	.query_port = mlx5_ib_rep_query_port,
	.query_pkey = mlx5_ib_rep_query_pkey,
};

static int mlx5_ib_stage_raw_eth_non_default_cb(struct mlx5_ib_dev *dev)
{
	ib_set_device_ops(&dev->ib_dev, &mlx5_ib_dev_port_rep_ops);
	return 0;
}

static const struct ib_device_ops mlx5_ib_dev_common_roce_ops = {
	.create_rwq_ind_table = mlx5_ib_create_rwq_ind_table,
	.create_wq = mlx5_ib_create_wq,
	.destroy_rwq_ind_table = mlx5_ib_destroy_rwq_ind_table,
	.destroy_wq = mlx5_ib_destroy_wq,
	.modify_wq = mlx5_ib_modify_wq,

	INIT_RDMA_OBJ_SIZE(ib_rwq_ind_table, mlx5_ib_rwq_ind_table,
			   ib_rwq_ind_tbl),
};

static int mlx5_ib_roce_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_core_dev *mdev = dev->mdev;
	enum rdma_link_layer ll;
	int port_type_cap;
	u32 port_num = 0;
	int err;

	port_type_cap = MLX5_CAP_GEN(mdev, port_type);
	ll = mlx5_port_type_cap_to_rdma_ll(port_type_cap);

	if (ll == IB_LINK_LAYER_ETHERNET) {
		ib_set_device_ops(&dev->ib_dev, &mlx5_ib_dev_common_roce_ops);

		port_num = mlx5_core_native_port_num(dev->mdev) - 1;

		/* Register only for native ports */
		mlx5_mdev_netdev_track(dev, port_num);

		err = mlx5_enable_eth(dev);
		if (err)
			goto cleanup;
	}

	return 0;
cleanup:
	mlx5_mdev_netdev_untrack(dev, port_num);
	return err;
}

static void mlx5_ib_roce_cleanup(struct mlx5_ib_dev *dev)
{
	struct mlx5_core_dev *mdev = dev->mdev;
	enum rdma_link_layer ll;
	int port_type_cap;
	u32 port_num;

	port_type_cap = MLX5_CAP_GEN(mdev, port_type);
	ll = mlx5_port_type_cap_to_rdma_ll(port_type_cap);

	if (ll == IB_LINK_LAYER_ETHERNET) {
		mlx5_disable_eth(dev);

		port_num = mlx5_core_native_port_num(dev->mdev) - 1;
		mlx5_mdev_netdev_untrack(dev, port_num);
	}
}

static int mlx5_ib_stage_cong_debugfs_init(struct mlx5_ib_dev *dev)
{
	mlx5_ib_init_cong_debugfs(dev,
				  mlx5_core_native_port_num(dev->mdev) - 1);
	return 0;
}

static void mlx5_ib_stage_cong_debugfs_cleanup(struct mlx5_ib_dev *dev)
{
	mlx5_ib_cleanup_cong_debugfs(dev,
				     mlx5_core_native_port_num(dev->mdev) - 1);
}

static int mlx5_ib_stage_uar_init(struct mlx5_ib_dev *dev)
{
	dev->mdev->priv.uar = mlx5_get_uars_page(dev->mdev);
	return PTR_ERR_OR_ZERO(dev->mdev->priv.uar);
}

static void mlx5_ib_stage_uar_cleanup(struct mlx5_ib_dev *dev)
{
	mlx5_put_uars_page(dev->mdev, dev->mdev->priv.uar);
}

static int mlx5_ib_stage_bfrag_init(struct mlx5_ib_dev *dev)
{
	int err;

	err = mlx5_alloc_bfreg(dev->mdev, &dev->bfreg, false, false);
	if (err)
		return err;

	err = mlx5_alloc_bfreg(dev->mdev, &dev->fp_bfreg, false, true);
	if (err)
		mlx5_free_bfreg(dev->mdev, &dev->bfreg);

	return err;
}

static void mlx5_ib_stage_bfrag_cleanup(struct mlx5_ib_dev *dev)
{
	mlx5_free_bfreg(dev->mdev, &dev->fp_bfreg);
	mlx5_free_bfreg(dev->mdev, &dev->bfreg);
}

static int mlx5_ib_stage_ib_reg_init(struct mlx5_ib_dev *dev)
{
	const char *name;

	if (dev->sub_dev_name) {
		name = dev->sub_dev_name;
		ib_mark_name_assigned_by_user(&dev->ib_dev);
	} else if (!mlx5_lag_is_active(dev->mdev))
		name = "mlx5_%d";
	else
		name = "mlx5_bond_%d";
	return ib_register_device(&dev->ib_dev, name, &dev->mdev->pdev->dev);
}

static void mlx5_ib_stage_pre_ib_reg_umr_cleanup(struct mlx5_ib_dev *dev)
{
	mlx5_mkey_cache_cleanup(dev);
	mlx5r_umr_resource_cleanup(dev);
	mlx5r_umr_cleanup(dev);
}

static void mlx5_ib_stage_ib_reg_cleanup(struct mlx5_ib_dev *dev)
{
	ib_unregister_device(&dev->ib_dev);
}

static int mlx5_ib_stage_post_ib_reg_umr_init(struct mlx5_ib_dev *dev)
{
	int ret;

	ret = mlx5r_umr_init(dev);
	if (ret)
		return ret;

	ret = mlx5_mkey_cache_init(dev);
	if (ret)
		mlx5_ib_warn(dev, "mr cache init failed %d\n", ret);
	return ret;
}

static int mlx5_ib_stage_delay_drop_init(struct mlx5_ib_dev *dev)
{
	struct dentry *root;

	if (!(dev->ib_dev.attrs.raw_packet_caps & IB_RAW_PACKET_CAP_DELAY_DROP))
		return 0;

	mutex_init(&dev->delay_drop.lock);
	dev->delay_drop.dev = dev;
	dev->delay_drop.activate = false;
	dev->delay_drop.timeout = MLX5_MAX_DELAY_DROP_TIMEOUT_MS * 1000;
	INIT_WORK(&dev->delay_drop.delay_drop_work, delay_drop_handler);
	atomic_set(&dev->delay_drop.rqs_cnt, 0);
	atomic_set(&dev->delay_drop.events_cnt, 0);

	if (!mlx5_debugfs_root)
		return 0;

	root = debugfs_create_dir("delay_drop", mlx5_debugfs_get_dev_root(dev->mdev));
	dev->delay_drop.dir_debugfs = root;

	debugfs_create_atomic_t("num_timeout_events", 0400, root,
				&dev->delay_drop.events_cnt);
	debugfs_create_atomic_t("num_rqs", 0400, root,
				&dev->delay_drop.rqs_cnt);
	debugfs_create_file("timeout", 0600, root, &dev->delay_drop,
			    &fops_delay_drop_timeout);
	return 0;
}

static void mlx5_ib_stage_delay_drop_cleanup(struct mlx5_ib_dev *dev)
{
	if (!(dev->ib_dev.attrs.raw_packet_caps & IB_RAW_PACKET_CAP_DELAY_DROP))
		return;

	cancel_work_sync(&dev->delay_drop.delay_drop_work);
	if (!dev->delay_drop.dir_debugfs)
		return;

	debugfs_remove_recursive(dev->delay_drop.dir_debugfs);
	dev->delay_drop.dir_debugfs = NULL;
}

static int mlx5_ib_stage_dev_notifier_init(struct mlx5_ib_dev *dev)
{
	struct mlx5_ib_resources *devr = &dev->devr;
	int port;

	for (port = 0; port < ARRAY_SIZE(devr->ports); ++port)
		INIT_WORK(&devr->ports[port].pkey_change_work,
			  pkey_change_handler);

	dev->mdev_events.notifier_call = mlx5_ib_event;
	mlx5_notifier_register(dev->mdev, &dev->mdev_events);

	mlx5r_macsec_event_register(dev);

	return 0;
}

static void mlx5_ib_stage_dev_notifier_cleanup(struct mlx5_ib_dev *dev)
{
	struct mlx5_ib_resources *devr = &dev->devr;
	int port;

	mlx5r_macsec_event_unregister(dev);
	mlx5_notifier_unregister(dev->mdev, &dev->mdev_events);

	for (port = 0; port < ARRAY_SIZE(devr->ports); ++port)
		cancel_work_sync(&devr->ports[port].pkey_change_work);
}

void mlx5_ib_data_direct_bind(struct mlx5_ib_dev *ibdev,
			      struct mlx5_data_direct_dev *dev)
{
	mutex_lock(&ibdev->data_direct_lock);
	ibdev->data_direct_dev = dev;
	mutex_unlock(&ibdev->data_direct_lock);
}

void mlx5_ib_data_direct_unbind(struct mlx5_ib_dev *ibdev)
{
	mutex_lock(&ibdev->data_direct_lock);
	mlx5_ib_revoke_data_direct_mrs(ibdev);
	ibdev->data_direct_dev = NULL;
	mutex_unlock(&ibdev->data_direct_lock);
}

void __mlx5_ib_remove(struct mlx5_ib_dev *dev,
		      const struct mlx5_ib_profile *profile,
		      int stage)
{
	dev->ib_active = false;

	/* Number of stages to cleanup */
	while (stage) {
		stage--;
		if (profile->stage[stage].cleanup)
			profile->stage[stage].cleanup(dev);
	}

	kfree(dev->port);
	ib_dealloc_device(&dev->ib_dev);
}

int __mlx5_ib_add(struct mlx5_ib_dev *dev,
		  const struct mlx5_ib_profile *profile)
{
	int err;
	int i;

	dev->profile = profile;

	for (i = 0; i < MLX5_IB_STAGE_MAX; i++) {
		if (profile->stage[i].init) {
			err = profile->stage[i].init(dev);
			if (err)
				goto err_out;
		}
	}

	dev->ib_active = true;
	return 0;

err_out:
	/* Clean up stages which were initialized */
	while (i) {
		i--;
		if (profile->stage[i].cleanup)
			profile->stage[i].cleanup(dev);
	}
	return -ENOMEM;
}

static const struct mlx5_ib_profile pf_profile = {
	STAGE_CREATE(MLX5_IB_STAGE_INIT,
		     mlx5_ib_stage_init_init,
		     mlx5_ib_stage_init_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_FS,
		     mlx5_ib_fs_init,
		     mlx5_ib_fs_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_CAPS,
		     mlx5_ib_stage_caps_init,
		     mlx5_ib_stage_caps_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_NON_DEFAULT_CB,
		     mlx5_ib_stage_non_default_cb,
		     NULL),
	STAGE_CREATE(MLX5_IB_STAGE_ROCE,
		     mlx5_ib_roce_init,
		     mlx5_ib_roce_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_QP,
		     mlx5_init_qp_table,
		     mlx5_cleanup_qp_table),
	STAGE_CREATE(MLX5_IB_STAGE_SRQ,
		     mlx5_init_srq_table,
		     mlx5_cleanup_srq_table),
	STAGE_CREATE(MLX5_IB_STAGE_DEVICE_RESOURCES,
		     mlx5_ib_dev_res_init,
		     mlx5_ib_dev_res_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_ODP,
		     mlx5_ib_odp_init_one,
		     mlx5_ib_odp_cleanup_one),
	STAGE_CREATE(MLX5_IB_STAGE_COUNTERS,
		     mlx5_ib_counters_init,
		     mlx5_ib_counters_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_CONG_DEBUGFS,
		     mlx5_ib_stage_cong_debugfs_init,
		     mlx5_ib_stage_cong_debugfs_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_UAR,
		     mlx5_ib_stage_uar_init,
		     mlx5_ib_stage_uar_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_BFREG,
		     mlx5_ib_stage_bfrag_init,
		     mlx5_ib_stage_bfrag_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_PRE_IB_REG_UMR,
		     NULL,
		     mlx5_ib_stage_pre_ib_reg_umr_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_WHITELIST_UID,
		     mlx5_ib_devx_init,
		     mlx5_ib_devx_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_IB_REG,
		     mlx5_ib_stage_ib_reg_init,
		     mlx5_ib_stage_ib_reg_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_DEVICE_NOTIFIER,
		     mlx5_ib_stage_dev_notifier_init,
		     mlx5_ib_stage_dev_notifier_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_POST_IB_REG_UMR,
		     mlx5_ib_stage_post_ib_reg_umr_init,
		     NULL),
	STAGE_CREATE(MLX5_IB_STAGE_DELAY_DROP,
		     mlx5_ib_stage_delay_drop_init,
		     mlx5_ib_stage_delay_drop_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_RESTRACK,
		     mlx5_ib_restrack_init,
		     NULL),
};

const struct mlx5_ib_profile raw_eth_profile = {
	STAGE_CREATE(MLX5_IB_STAGE_INIT,
		     mlx5_ib_stage_init_init,
		     mlx5_ib_stage_init_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_FS,
		     mlx5_ib_fs_init,
		     mlx5_ib_fs_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_CAPS,
		     mlx5_ib_stage_caps_init,
		     mlx5_ib_stage_caps_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_NON_DEFAULT_CB,
		     mlx5_ib_stage_raw_eth_non_default_cb,
		     NULL),
	STAGE_CREATE(MLX5_IB_STAGE_ROCE,
		     mlx5_ib_roce_init,
		     mlx5_ib_roce_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_QP,
		     mlx5_init_qp_table,
		     mlx5_cleanup_qp_table),
	STAGE_CREATE(MLX5_IB_STAGE_SRQ,
		     mlx5_init_srq_table,
		     mlx5_cleanup_srq_table),
	STAGE_CREATE(MLX5_IB_STAGE_DEVICE_RESOURCES,
		     mlx5_ib_dev_res_init,
		     mlx5_ib_dev_res_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_COUNTERS,
		     mlx5_ib_counters_init,
		     mlx5_ib_counters_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_CONG_DEBUGFS,
		     mlx5_ib_stage_cong_debugfs_init,
		     mlx5_ib_stage_cong_debugfs_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_UAR,
		     mlx5_ib_stage_uar_init,
		     mlx5_ib_stage_uar_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_BFREG,
		     mlx5_ib_stage_bfrag_init,
		     mlx5_ib_stage_bfrag_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_PRE_IB_REG_UMR,
		     NULL,
		     mlx5_ib_stage_pre_ib_reg_umr_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_WHITELIST_UID,
		     mlx5_ib_devx_init,
		     mlx5_ib_devx_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_IB_REG,
		     mlx5_ib_stage_ib_reg_init,
		     mlx5_ib_stage_ib_reg_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_DEVICE_NOTIFIER,
		     mlx5_ib_stage_dev_notifier_init,
		     mlx5_ib_stage_dev_notifier_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_POST_IB_REG_UMR,
		     mlx5_ib_stage_post_ib_reg_umr_init,
		     NULL),
	STAGE_CREATE(MLX5_IB_STAGE_DELAY_DROP,
		     mlx5_ib_stage_delay_drop_init,
		     mlx5_ib_stage_delay_drop_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_RESTRACK,
		     mlx5_ib_restrack_init,
		     NULL),
};

static const struct mlx5_ib_profile plane_profile = {
	STAGE_CREATE(MLX5_IB_STAGE_INIT,
		     mlx5_ib_stage_init_init,
		     mlx5_ib_stage_init_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_CAPS,
		     mlx5_ib_stage_caps_init,
		     mlx5_ib_stage_caps_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_NON_DEFAULT_CB,
		     mlx5_ib_stage_non_default_cb,
		     NULL),
	STAGE_CREATE(MLX5_IB_STAGE_QP,
		     mlx5_init_qp_table,
		     mlx5_cleanup_qp_table),
	STAGE_CREATE(MLX5_IB_STAGE_SRQ,
		     mlx5_init_srq_table,
		     mlx5_cleanup_srq_table),
	STAGE_CREATE(MLX5_IB_STAGE_DEVICE_RESOURCES,
		     mlx5_ib_dev_res_init,
		     mlx5_ib_dev_res_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_BFREG,
		     mlx5_ib_stage_bfrag_init,
		     mlx5_ib_stage_bfrag_cleanup),
	STAGE_CREATE(MLX5_IB_STAGE_IB_REG,
		     mlx5_ib_stage_ib_reg_init,
		     mlx5_ib_stage_ib_reg_cleanup),
};

static struct ib_device *mlx5_ib_add_sub_dev(struct ib_device *parent,
					     enum rdma_nl_dev_type type,
					     const char *name)
{
	struct mlx5_ib_dev *mparent = to_mdev(parent), *mplane;
	enum rdma_link_layer ll;
	int ret;

	if (mparent->smi_dev)
		return ERR_PTR(-EEXIST);

	ll = mlx5_port_type_cap_to_rdma_ll(MLX5_CAP_GEN(mparent->mdev,
							port_type));
	if (type != RDMA_DEVICE_TYPE_SMI || !mparent->num_plane ||
	    ll != IB_LINK_LAYER_INFINIBAND ||
	    !MLX5_CAP_GEN_2(mparent->mdev, multiplane_qp_ud))
		return ERR_PTR(-EOPNOTSUPP);

	mplane = ib_alloc_device(mlx5_ib_dev, ib_dev);
	if (!mplane)
		return ERR_PTR(-ENOMEM);

	mplane->port = kcalloc(mparent->num_plane * mparent->num_ports,
			       sizeof(*mplane->port), GFP_KERNEL);
	if (!mplane->port) {
		ret = -ENOMEM;
		goto fail_kcalloc;
	}

	mplane->ib_dev.type = type;
	mplane->mdev = mparent->mdev;
	mplane->num_ports = mparent->num_plane;
	mplane->sub_dev_name = name;
	mplane->ib_dev.phys_port_cnt = mplane->num_ports;

	ret = __mlx5_ib_add(mplane, &plane_profile);
	if (ret)
		goto fail_ib_add;

	mparent->smi_dev = mplane;
	return &mplane->ib_dev;

fail_ib_add:
	kfree(mplane->port);
fail_kcalloc:
	ib_dealloc_device(&mplane->ib_dev);
	return ERR_PTR(ret);
}

static void mlx5_ib_del_sub_dev(struct ib_device *sub_dev)
{
	struct mlx5_ib_dev *mdev = to_mdev(sub_dev);

	to_mdev(sub_dev->parent)->smi_dev = NULL;
	__mlx5_ib_remove(mdev, mdev->profile, MLX5_IB_STAGE_MAX);
}

static int mlx5r_mp_probe(struct auxiliary_device *adev,
			  const struct auxiliary_device_id *id)
{
	struct mlx5_adev *idev = container_of(adev, struct mlx5_adev, adev);
	struct mlx5_core_dev *mdev = idev->mdev;
	struct mlx5_ib_multiport_info *mpi;
	struct mlx5_ib_dev *dev;
	bool bound = false;
	int err;

	mpi = kzalloc(sizeof(*mpi), GFP_KERNEL);
	if (!mpi)
		return -ENOMEM;

	mpi->mdev = mdev;
	err = mlx5_query_nic_vport_system_image_guid(mdev,
						     &mpi->sys_image_guid);
	if (err) {
		kfree(mpi);
		return err;
	}

	mutex_lock(&mlx5_ib_multiport_mutex);
	list_for_each_entry(dev, &mlx5_ib_dev_list, ib_dev_list) {
		if (dev->sys_image_guid == mpi->sys_image_guid &&
		    mlx5_core_same_coredev_type(dev->mdev, mpi->mdev))
			bound = mlx5_ib_bind_slave_port(dev, mpi);

		if (bound) {
			rdma_roce_rescan_device(&dev->ib_dev);
			mpi->ibdev->ib_active = true;
			break;
		}
	}

	if (!bound) {
		list_add_tail(&mpi->list, &mlx5_ib_unaffiliated_port_list);
		dev_dbg(mdev->device,
			"no suitable IB device found to bind to, added to unaffiliated list.\n");
	}
	mutex_unlock(&mlx5_ib_multiport_mutex);

	auxiliary_set_drvdata(adev, mpi);
	return 0;
}

static void mlx5r_mp_remove(struct auxiliary_device *adev)
{
	struct mlx5_ib_multiport_info *mpi;

	mpi = auxiliary_get_drvdata(adev);
	mutex_lock(&mlx5_ib_multiport_mutex);
	if (mpi->ibdev)
		mlx5_ib_unbind_slave_port(mpi->ibdev, mpi);
	else
		list_del(&mpi->list);
	mutex_unlock(&mlx5_ib_multiport_mutex);
	kfree(mpi);
}

static int mlx5r_probe(struct auxiliary_device *adev,
		       const struct auxiliary_device_id *id)
{
	struct mlx5_adev *idev = container_of(adev, struct mlx5_adev, adev);
	struct mlx5_core_dev *mdev = idev->mdev;
	const struct mlx5_ib_profile *profile;
	int port_type_cap, num_ports, ret;
	enum rdma_link_layer ll;
	struct mlx5_ib_dev *dev;

	port_type_cap = MLX5_CAP_GEN(mdev, port_type);
	ll = mlx5_port_type_cap_to_rdma_ll(port_type_cap);

	num_ports = max(MLX5_CAP_GEN(mdev, num_ports),
			MLX5_CAP_GEN(mdev, num_vhca_ports));
	dev = ib_alloc_device(mlx5_ib_dev, ib_dev);
	if (!dev)
		return -ENOMEM;

	if (ll == IB_LINK_LAYER_INFINIBAND) {
		ret = mlx5_ib_get_plane_num(mdev, &dev->num_plane);
		if (ret)
			goto fail;
	}

	dev->port = kcalloc(num_ports, sizeof(*dev->port),
			     GFP_KERNEL);
	if (!dev->port) {
		ret = -ENOMEM;
		goto fail;
	}

	dev->mdev = mdev;
	dev->num_ports = num_ports;
	dev->ib_dev.phys_port_cnt = num_ports;

	if (ll == IB_LINK_LAYER_ETHERNET && !mlx5_get_roce_state(mdev))
		profile = &raw_eth_profile;
	else
		profile = &pf_profile;

	ret = __mlx5_ib_add(dev, profile);
	if (ret)
		goto fail_ib_add;

	auxiliary_set_drvdata(adev, dev);
	return 0;

fail_ib_add:
	kfree(dev->port);
fail:
	ib_dealloc_device(&dev->ib_dev);
	return ret;
}

static void mlx5r_remove(struct auxiliary_device *adev)
{
	struct mlx5_ib_dev *dev;

	dev = auxiliary_get_drvdata(adev);
	__mlx5_ib_remove(dev, dev->profile, MLX5_IB_STAGE_MAX);
}

static const struct auxiliary_device_id mlx5r_mp_id_table[] = {
	{ .name = MLX5_ADEV_NAME ".multiport", },
	{},
};

static const struct auxiliary_device_id mlx5r_id_table[] = {
	{ .name = MLX5_ADEV_NAME ".rdma", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, mlx5r_mp_id_table);
MODULE_DEVICE_TABLE(auxiliary, mlx5r_id_table);

static struct auxiliary_driver mlx5r_mp_driver = {
	.name = "multiport",
	.probe = mlx5r_mp_probe,
	.remove = mlx5r_mp_remove,
	.id_table = mlx5r_mp_id_table,
};

static struct auxiliary_driver mlx5r_driver = {
	.name = "rdma",
	.probe = mlx5r_probe,
	.remove = mlx5r_remove,
	.id_table = mlx5r_id_table,
};

static int __init mlx5_ib_init(void)
{
	int ret;

	xlt_emergency_page = (void *)__get_free_page(GFP_KERNEL);
	if (!xlt_emergency_page)
		return -ENOMEM;

	mlx5_ib_event_wq = alloc_ordered_workqueue("mlx5_ib_event_wq", 0);
	if (!mlx5_ib_event_wq) {
		free_page((unsigned long)xlt_emergency_page);
		return -ENOMEM;
	}

	ret = mlx5_ib_qp_event_init();
	if (ret)
		goto qp_event_err;

	mlx5_ib_odp_init();
	ret = mlx5r_rep_init();
	if (ret)
		goto rep_err;
	ret = mlx5_data_direct_driver_register();
	if (ret)
		goto dd_err;
	ret = auxiliary_driver_register(&mlx5r_mp_driver);
	if (ret)
		goto mp_err;
	ret = auxiliary_driver_register(&mlx5r_driver);
	if (ret)
		goto drv_err;

	return 0;

drv_err:
	auxiliary_driver_unregister(&mlx5r_mp_driver);
mp_err:
	mlx5_data_direct_driver_unregister();
dd_err:
	mlx5r_rep_cleanup();
rep_err:
	mlx5_ib_qp_event_cleanup();
qp_event_err:
	destroy_workqueue(mlx5_ib_event_wq);
	free_page((unsigned long)xlt_emergency_page);
	return ret;
}

static void __exit mlx5_ib_cleanup(void)
{
	mlx5_data_direct_driver_unregister();
	auxiliary_driver_unregister(&mlx5r_driver);
	auxiliary_driver_unregister(&mlx5r_mp_driver);
	mlx5r_rep_cleanup();

	mlx5_ib_qp_event_cleanup();
	destroy_workqueue(mlx5_ib_event_wq);
	free_page((unsigned long)xlt_emergency_page);
}

module_init(mlx5_ib_init);
module_exit(mlx5_ib_cleanup);
