// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2016 Mellanox Technologies Ltd. All rights reserved.
 * Copyright (c) 2015 System Fabric Works, Inc. All rights reserved.
 */

#include <rdma/rdma_netlink.h>
#include <net/addrconf.h>
#include "rxe.h"
#include "rxe_loc.h"

MODULE_AUTHOR("Bob Pearson, Frank Zago, John Groves, Kamal Heib");
MODULE_DESCRIPTION("Soft RDMA transport");
MODULE_LICENSE("Dual BSD/GPL");

/* free resources for a rxe device all objects created for this device must
 * have been destroyed
 */
void rxe_dealloc(struct ib_device *ib_dev)
{
	struct rxe_dev *rxe = container_of(ib_dev, struct rxe_dev, ib_dev);

	rxe_pool_cleanup(&rxe->uc_pool);
	rxe_pool_cleanup(&rxe->pd_pool);
	rxe_pool_cleanup(&rxe->ah_pool);
	rxe_pool_cleanup(&rxe->srq_pool);
	rxe_pool_cleanup(&rxe->qp_pool);
	rxe_pool_cleanup(&rxe->cq_pool);
	rxe_pool_cleanup(&rxe->mr_pool);
	rxe_pool_cleanup(&rxe->mw_pool);

	WARN_ON(!RB_EMPTY_ROOT(&rxe->mcg_tree));

	if (rxe->tfm)
		crypto_free_shash(rxe->tfm);

	mutex_destroy(&rxe->usdev_lock);
}

/* initialize rxe device parameters */
static void rxe_init_device_param(struct rxe_dev *rxe, struct net_device *ndev)
{
	rxe->max_inline_data			= RXE_MAX_INLINE_DATA;

	rxe->attr.vendor_id			= RXE_VENDOR_ID;
	rxe->attr.max_mr_size			= RXE_MAX_MR_SIZE;
	rxe->attr.page_size_cap			= RXE_PAGE_SIZE_CAP;
	rxe->attr.max_qp			= RXE_MAX_QP;
	rxe->attr.max_qp_wr			= RXE_MAX_QP_WR;
	rxe->attr.device_cap_flags		= RXE_DEVICE_CAP_FLAGS;
	rxe->attr.kernel_cap_flags		= IBK_ALLOW_USER_UNREG;
	rxe->attr.max_send_sge			= RXE_MAX_SGE;
	rxe->attr.max_recv_sge			= RXE_MAX_SGE;
	rxe->attr.max_sge_rd			= RXE_MAX_SGE_RD;
	rxe->attr.max_cq			= RXE_MAX_CQ;
	rxe->attr.max_cqe			= (1 << RXE_MAX_LOG_CQE) - 1;
	rxe->attr.max_mr			= RXE_MAX_MR;
	rxe->attr.max_mw			= RXE_MAX_MW;
	rxe->attr.max_pd			= RXE_MAX_PD;
	rxe->attr.max_qp_rd_atom		= RXE_MAX_QP_RD_ATOM;
	rxe->attr.max_res_rd_atom		= RXE_MAX_RES_RD_ATOM;
	rxe->attr.max_qp_init_rd_atom		= RXE_MAX_QP_INIT_RD_ATOM;
	rxe->attr.atomic_cap			= IB_ATOMIC_HCA;
	rxe->attr.max_mcast_grp			= RXE_MAX_MCAST_GRP;
	rxe->attr.max_mcast_qp_attach		= RXE_MAX_MCAST_QP_ATTACH;
	rxe->attr.max_total_mcast_qp_attach	= RXE_MAX_TOT_MCAST_QP_ATTACH;
	rxe->attr.max_ah			= RXE_MAX_AH;
	rxe->attr.max_srq			= RXE_MAX_SRQ;
	rxe->attr.max_srq_wr			= RXE_MAX_SRQ_WR;
	rxe->attr.max_srq_sge			= RXE_MAX_SRQ_SGE;
	rxe->attr.max_fast_reg_page_list_len	= RXE_MAX_FMR_PAGE_LIST_LEN;
	rxe->attr.max_pkeys			= RXE_MAX_PKEYS;
	rxe->attr.local_ca_ack_delay		= RXE_LOCAL_CA_ACK_DELAY;

	addrconf_addr_eui48((unsigned char *)&rxe->attr.sys_image_guid,
			ndev->dev_addr);

	rxe->max_ucontext			= RXE_MAX_UCONTEXT;
}

/* initialize port attributes */
static void rxe_init_port_param(struct rxe_port *port)
{
	port->attr.state		= IB_PORT_DOWN;
	port->attr.max_mtu		= IB_MTU_4096;
	port->attr.active_mtu		= IB_MTU_256;
	port->attr.gid_tbl_len		= RXE_PORT_GID_TBL_LEN;
	port->attr.port_cap_flags	= RXE_PORT_PORT_CAP_FLAGS;
	port->attr.max_msg_sz		= RXE_PORT_MAX_MSG_SZ;
	port->attr.bad_pkey_cntr	= RXE_PORT_BAD_PKEY_CNTR;
	port->attr.qkey_viol_cntr	= RXE_PORT_QKEY_VIOL_CNTR;
	port->attr.pkey_tbl_len		= RXE_PORT_PKEY_TBL_LEN;
	port->attr.lid			= RXE_PORT_LID;
	port->attr.sm_lid		= RXE_PORT_SM_LID;
	port->attr.lmc			= RXE_PORT_LMC;
	port->attr.max_vl_num		= RXE_PORT_MAX_VL_NUM;
	port->attr.sm_sl		= RXE_PORT_SM_SL;
	port->attr.subnet_timeout	= RXE_PORT_SUBNET_TIMEOUT;
	port->attr.init_type_reply	= RXE_PORT_INIT_TYPE_REPLY;
	port->attr.active_width		= RXE_PORT_ACTIVE_WIDTH;
	port->attr.active_speed		= RXE_PORT_ACTIVE_SPEED;
	port->attr.phys_state		= RXE_PORT_PHYS_STATE;
	port->mtu_cap			= ib_mtu_enum_to_int(IB_MTU_256);
	port->subnet_prefix		= cpu_to_be64(RXE_PORT_SUBNET_PREFIX);
}

/* initialize port state, note IB convention that HCA ports are always
 * numbered from 1
 */
static void rxe_init_ports(struct rxe_dev *rxe, struct net_device *ndev)
{
	struct rxe_port *port = &rxe->port;

	rxe_init_port_param(port);
	addrconf_addr_eui48((unsigned char *)&port->port_guid,
			    ndev->dev_addr);
	spin_lock_init(&port->port_lock);
}

/* init pools of managed objects */
static void rxe_init_pools(struct rxe_dev *rxe)
{
	rxe_pool_init(rxe, &rxe->uc_pool, RXE_TYPE_UC);
	rxe_pool_init(rxe, &rxe->pd_pool, RXE_TYPE_PD);
	rxe_pool_init(rxe, &rxe->ah_pool, RXE_TYPE_AH);
	rxe_pool_init(rxe, &rxe->srq_pool, RXE_TYPE_SRQ);
	rxe_pool_init(rxe, &rxe->qp_pool, RXE_TYPE_QP);
	rxe_pool_init(rxe, &rxe->cq_pool, RXE_TYPE_CQ);
	rxe_pool_init(rxe, &rxe->mr_pool, RXE_TYPE_MR);
	rxe_pool_init(rxe, &rxe->mw_pool, RXE_TYPE_MW);
}

/* initialize rxe device state */
static void rxe_init(struct rxe_dev *rxe, struct net_device *ndev)
{
	/* init default device parameters */
	rxe_init_device_param(rxe, ndev);

	rxe_init_ports(rxe, ndev);
	rxe_init_pools(rxe);

	/* init pending mmap list */
	spin_lock_init(&rxe->mmap_offset_lock);
	spin_lock_init(&rxe->pending_lock);
	INIT_LIST_HEAD(&rxe->pending_mmaps);

	/* init multicast support */
	spin_lock_init(&rxe->mcg_lock);
	rxe->mcg_tree = RB_ROOT;

	mutex_init(&rxe->usdev_lock);
}

void rxe_set_mtu(struct rxe_dev *rxe, unsigned int ndev_mtu)
{
	struct rxe_port *port = &rxe->port;
	enum ib_mtu mtu;

	mtu = eth_mtu_int_to_enum(ndev_mtu);

	/* Make sure that new MTU in range */
	mtu = mtu ? min_t(enum ib_mtu, mtu, IB_MTU_4096) : IB_MTU_256;

	port->attr.active_mtu = mtu;
	port->mtu_cap = ib_mtu_enum_to_int(mtu);
}

/* called by ifc layer to create new rxe device.
 * The caller should allocate memory for rxe by calling ib_alloc_device.
 */
int rxe_add(struct rxe_dev *rxe, unsigned int mtu, const char *ibdev_name,
			struct net_device *ndev)
{
	rxe_init(rxe, ndev);
	rxe_set_mtu(rxe, mtu);

	return rxe_register_device(rxe, ibdev_name, ndev);
}

static int rxe_newlink(const char *ibdev_name, struct net_device *ndev)
{
	struct rxe_dev *rxe;
	int err = 0;

	if (is_vlan_dev(ndev)) {
		rxe_err("rxe creation allowed on top of a real device only\n");
		err = -EPERM;
		goto err;
	}

	rxe = rxe_get_dev_from_net(ndev);
	if (rxe) {
		ib_device_put(&rxe->ib_dev);
		rxe_err_dev(rxe, "already configured on %s\n", ndev->name);
		err = -EEXIST;
		goto err;
	}

	err = rxe_net_add(ibdev_name, ndev);
	if (err) {
		rxe_err("failed to add %s\n", ndev->name);
		goto err;
	}
err:
	return err;
}

static struct rdma_link_ops rxe_link_ops = {
	.type = "rxe",
	.newlink = rxe_newlink,
};

static int __init rxe_module_init(void)
{
	int err;

	err = rxe_alloc_wq();
	if (err)
		return err;

	err = rxe_net_init();
	if (err) {
		rxe_destroy_wq();
		return err;
	}

	rdma_link_register(&rxe_link_ops);
	pr_info("loaded\n");
	return 0;
}

static void __exit rxe_module_exit(void)
{
	rdma_link_unregister(&rxe_link_ops);
	ib_unregister_driver(RDMA_DRIVER_RXE);
	rxe_net_exit();
	rxe_destroy_wq();

	pr_info("unloaded\n");
}

late_initcall(rxe_module_init);
module_exit(rxe_module_exit);

MODULE_ALIAS_RDMA_LINK("rxe");
