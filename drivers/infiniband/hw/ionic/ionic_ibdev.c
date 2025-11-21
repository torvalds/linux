// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#include <linux/module.h>
#include <linux/printk.h>
#include <linux/pci.h>
#include <linux/irq.h>
#include <net/addrconf.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_mad.h>

#include "ionic_ibdev.h"

#define DRIVER_DESCRIPTION "AMD Pensando RoCE HCA driver"
#define DEVICE_DESCRIPTION "AMD Pensando RoCE HCA"

MODULE_AUTHOR("Allen Hubbe <allen.hubbe@amd.com>");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("NET_IONIC");

static int ionic_query_device(struct ib_device *ibdev,
			      struct ib_device_attr *attr,
			      struct ib_udata *udata)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibdev);
	struct net_device *ndev;

	ndev = ib_device_get_netdev(ibdev, 1);
	addrconf_ifid_eui48((u8 *)&attr->sys_image_guid, ndev);
	dev_put(ndev);
	attr->max_mr_size = dev->lif_cfg.npts_per_lif * PAGE_SIZE / 2;
	attr->page_size_cap = dev->lif_cfg.page_size_supported;

	attr->vendor_id = to_pci_dev(dev->lif_cfg.hwdev)->vendor;
	attr->vendor_part_id = to_pci_dev(dev->lif_cfg.hwdev)->device;

	attr->hw_ver = ionic_lif_asic_rev(dev->lif_cfg.lif);
	attr->fw_ver = 0;
	attr->max_qp = dev->lif_cfg.qp_count;
	attr->max_qp_wr = IONIC_MAX_DEPTH;
	attr->device_cap_flags =
		IB_DEVICE_MEM_WINDOW |
		IB_DEVICE_MEM_MGT_EXTENSIONS |
		IB_DEVICE_MEM_WINDOW_TYPE_2B |
		0;
	attr->max_send_sge =
		min(ionic_v1_send_wqe_max_sge(dev->lif_cfg.max_stride, 0, false),
		    IONIC_SPEC_HIGH);
	attr->max_recv_sge =
		min(ionic_v1_recv_wqe_max_sge(dev->lif_cfg.max_stride, 0, false),
		    IONIC_SPEC_HIGH);
	attr->max_sge_rd = attr->max_send_sge;
	attr->max_cq = dev->lif_cfg.cq_count / dev->lif_cfg.udma_count;
	attr->max_cqe = IONIC_MAX_CQ_DEPTH - IONIC_CQ_GRACE;
	attr->max_mr = dev->lif_cfg.nmrs_per_lif;
	attr->max_pd = IONIC_MAX_PD;
	attr->max_qp_rd_atom = IONIC_MAX_RD_ATOM;
	attr->max_ee_rd_atom = 0;
	attr->max_res_rd_atom = IONIC_MAX_RD_ATOM;
	attr->max_qp_init_rd_atom = IONIC_MAX_RD_ATOM;
	attr->max_ee_init_rd_atom = 0;
	attr->atomic_cap = IB_ATOMIC_GLOB;
	attr->masked_atomic_cap = IB_ATOMIC_GLOB;
	attr->max_mw = dev->lif_cfg.nmrs_per_lif;
	attr->max_mcast_grp = 0;
	attr->max_mcast_qp_attach = 0;
	attr->max_ah = dev->lif_cfg.nahs_per_lif;
	attr->max_fast_reg_page_list_len = dev->lif_cfg.npts_per_lif / 2;
	attr->max_pkeys = IONIC_PKEY_TBL_LEN;

	return 0;
}

static int ionic_query_port(struct ib_device *ibdev, u32 port,
			    struct ib_port_attr *attr)
{
	struct net_device *ndev;

	if (port != 1)
		return -EINVAL;

	ndev = ib_device_get_netdev(ibdev, port);

	if (netif_running(ndev) && netif_carrier_ok(ndev)) {
		attr->state = IB_PORT_ACTIVE;
		attr->phys_state = IB_PORT_PHYS_STATE_LINK_UP;
	} else if (netif_running(ndev)) {
		attr->state = IB_PORT_DOWN;
		attr->phys_state = IB_PORT_PHYS_STATE_POLLING;
	} else {
		attr->state = IB_PORT_DOWN;
		attr->phys_state = IB_PORT_PHYS_STATE_DISABLED;
	}

	attr->max_mtu = iboe_get_mtu(ndev->max_mtu);
	attr->active_mtu = min(attr->max_mtu, iboe_get_mtu(ndev->mtu));
	attr->gid_tbl_len = IONIC_GID_TBL_LEN;
	attr->ip_gids = true;
	attr->port_cap_flags = 0;
	attr->max_msg_sz = 0x80000000;
	attr->pkey_tbl_len = IONIC_PKEY_TBL_LEN;
	attr->max_vl_num = 1;
	attr->subnet_prefix = 0xfe80000000000000ull;

	dev_put(ndev);

	return ib_get_eth_speed(ibdev, port,
				&attr->active_speed,
				&attr->active_width);
}

static enum rdma_link_layer ionic_get_link_layer(struct ib_device *ibdev,
						 u32 port)
{
	return IB_LINK_LAYER_ETHERNET;
}

static int ionic_query_pkey(struct ib_device *ibdev, u32 port, u16 index,
			    u16 *pkey)
{
	if (port != 1)
		return -EINVAL;

	if (index != 0)
		return -EINVAL;

	*pkey = IB_DEFAULT_PKEY_FULL;

	return 0;
}

static int ionic_modify_device(struct ib_device *ibdev, int mask,
			       struct ib_device_modify *attr)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibdev);

	if (mask & ~IB_DEVICE_MODIFY_NODE_DESC)
		return -EOPNOTSUPP;

	if (mask & IB_DEVICE_MODIFY_NODE_DESC)
		memcpy(dev->ibdev.node_desc, attr->node_desc,
		       IB_DEVICE_NODE_DESC_MAX);

	return 0;
}

static int ionic_get_port_immutable(struct ib_device *ibdev, u32 port,
				    struct ib_port_immutable *attr)
{
	if (port != 1)
		return -EINVAL;

	attr->core_cap_flags = RDMA_CORE_PORT_IBA_ROCE_UDP_ENCAP;

	attr->pkey_tbl_len = IONIC_PKEY_TBL_LEN;
	attr->gid_tbl_len = IONIC_GID_TBL_LEN;
	attr->max_mad_size = IB_MGMT_MAD_SIZE;

	return 0;
}

static void ionic_get_dev_fw_str(struct ib_device *ibdev, char *str)
{
	struct ionic_ibdev *dev = to_ionic_ibdev(ibdev);

	ionic_lif_fw_version(dev->lif_cfg.lif, str, IB_FW_VERSION_NAME_MAX);
}

static ssize_t hw_rev_show(struct device *device, struct device_attribute *attr,
			   char *buf)
{
	struct ionic_ibdev *dev =
		rdma_device_to_drv_device(device, struct ionic_ibdev, ibdev);

	return sysfs_emit(buf, "0x%x\n", ionic_lif_asic_rev(dev->lif_cfg.lif));
}
static DEVICE_ATTR_RO(hw_rev);

static ssize_t hca_type_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct ionic_ibdev *dev =
		rdma_device_to_drv_device(device, struct ionic_ibdev, ibdev);

	return sysfs_emit(buf, "%s\n", dev->ibdev.node_desc);
}
static DEVICE_ATTR_RO(hca_type);

static struct attribute *ionic_rdma_attributes[] = {
	&dev_attr_hw_rev.attr,
	&dev_attr_hca_type.attr,
	NULL
};

static const struct attribute_group ionic_rdma_attr_group = {
	.attrs = ionic_rdma_attributes,
};

static void ionic_disassociate_ucontext(struct ib_ucontext *ibcontext)
{
	/*
	 * Dummy define disassociate_ucontext so that it does not
	 * wait for user context before cleaning up hw resources.
	 */
}

static const struct ib_device_ops ionic_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_IONIC,
	.uverbs_abi_ver = IONIC_ABI_VERSION,

	.alloc_ucontext = ionic_alloc_ucontext,
	.dealloc_ucontext = ionic_dealloc_ucontext,
	.mmap = ionic_mmap,
	.mmap_free = ionic_mmap_free,
	.alloc_pd = ionic_alloc_pd,
	.dealloc_pd = ionic_dealloc_pd,
	.create_ah = ionic_create_ah,
	.query_ah = ionic_query_ah,
	.destroy_ah = ionic_destroy_ah,
	.create_user_ah = ionic_create_ah,
	.get_dma_mr = ionic_get_dma_mr,
	.reg_user_mr = ionic_reg_user_mr,
	.reg_user_mr_dmabuf = ionic_reg_user_mr_dmabuf,
	.dereg_mr = ionic_dereg_mr,
	.alloc_mr = ionic_alloc_mr,
	.map_mr_sg = ionic_map_mr_sg,
	.alloc_mw = ionic_alloc_mw,
	.dealloc_mw = ionic_dealloc_mw,
	.create_cq = ionic_create_cq,
	.destroy_cq = ionic_destroy_cq,
	.create_qp = ionic_create_qp,
	.modify_qp = ionic_modify_qp,
	.query_qp = ionic_query_qp,
	.destroy_qp = ionic_destroy_qp,

	.post_send = ionic_post_send,
	.post_recv = ionic_post_recv,
	.poll_cq = ionic_poll_cq,
	.req_notify_cq = ionic_req_notify_cq,

	.query_device = ionic_query_device,
	.query_port = ionic_query_port,
	.get_link_layer = ionic_get_link_layer,
	.query_pkey = ionic_query_pkey,
	.modify_device = ionic_modify_device,
	.get_port_immutable = ionic_get_port_immutable,
	.get_dev_fw_str = ionic_get_dev_fw_str,
	.device_group = &ionic_rdma_attr_group,
	.disassociate_ucontext = ionic_disassociate_ucontext,

	INIT_RDMA_OBJ_SIZE(ib_ucontext, ionic_ctx, ibctx),
	INIT_RDMA_OBJ_SIZE(ib_pd, ionic_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_ah, ionic_ah, ibah),
	INIT_RDMA_OBJ_SIZE(ib_cq, ionic_vcq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_qp, ionic_qp, ibqp),
	INIT_RDMA_OBJ_SIZE(ib_mw, ionic_mr, ibmw),
};

static void ionic_init_resids(struct ionic_ibdev *dev)
{
	ionic_resid_init(&dev->inuse_cqid, dev->lif_cfg.cq_count);
	dev->half_cqid_udma_shift =
		order_base_2(dev->lif_cfg.cq_count / dev->lif_cfg.udma_count);
	ionic_resid_init(&dev->inuse_pdid, IONIC_MAX_PD);
	ionic_resid_init(&dev->inuse_ahid, dev->lif_cfg.nahs_per_lif);
	ionic_resid_init(&dev->inuse_mrid, dev->lif_cfg.nmrs_per_lif);
	/* skip reserved lkey */
	dev->next_mrkey = 1;
	ionic_resid_init(&dev->inuse_qpid, dev->lif_cfg.qp_count);
	/* skip reserved SMI and GSI qpids */
	dev->half_qpid_udma_shift =
		order_base_2(dev->lif_cfg.qp_count / dev->lif_cfg.udma_count);
	ionic_resid_init(&dev->inuse_dbid, dev->lif_cfg.dbid_count);
}

static void ionic_destroy_resids(struct ionic_ibdev *dev)
{
	ionic_resid_destroy(&dev->inuse_cqid);
	ionic_resid_destroy(&dev->inuse_pdid);
	ionic_resid_destroy(&dev->inuse_ahid);
	ionic_resid_destroy(&dev->inuse_mrid);
	ionic_resid_destroy(&dev->inuse_qpid);
	ionic_resid_destroy(&dev->inuse_dbid);
}

static void ionic_destroy_ibdev(struct ionic_ibdev *dev)
{
	ionic_kill_rdma_admin(dev, false);
	ib_unregister_device(&dev->ibdev);
	ionic_stats_cleanup(dev);
	ionic_destroy_rdma_admin(dev);
	ionic_destroy_resids(dev);
	WARN_ON(!xa_empty(&dev->qp_tbl));
	xa_destroy(&dev->qp_tbl);
	WARN_ON(!xa_empty(&dev->cq_tbl));
	xa_destroy(&dev->cq_tbl);
	ib_dealloc_device(&dev->ibdev);
}

static struct ionic_ibdev *ionic_create_ibdev(struct ionic_aux_dev *ionic_adev)
{
	struct ib_device *ibdev;
	struct ionic_ibdev *dev;
	struct net_device *ndev;
	int rc;

	dev = ib_alloc_device(ionic_ibdev, ibdev);
	if (!dev)
		return ERR_PTR(-EINVAL);

	ionic_fill_lif_cfg(ionic_adev->lif, &dev->lif_cfg);

	xa_init_flags(&dev->qp_tbl, GFP_ATOMIC);
	xa_init_flags(&dev->cq_tbl, GFP_ATOMIC);

	ionic_init_resids(dev);

	rc = ionic_rdma_reset_devcmd(dev);
	if (rc)
		goto err_reset;

	rc = ionic_create_rdma_admin(dev);
	if (rc)
		goto err_admin;

	ibdev = &dev->ibdev;
	ibdev->dev.parent = dev->lif_cfg.hwdev;

	strscpy(ibdev->name, "ionic_%d", IB_DEVICE_NAME_MAX);
	strscpy(ibdev->node_desc, DEVICE_DESCRIPTION, IB_DEVICE_NODE_DESC_MAX);

	ibdev->node_type = RDMA_NODE_IB_CA;
	ibdev->phys_port_cnt = 1;

	/* the first two eq are reserved for async events */
	ibdev->num_comp_vectors = dev->lif_cfg.eq_count - 2;

	ndev = ionic_lif_netdev(ionic_adev->lif);
	addrconf_ifid_eui48((u8 *)&ibdev->node_guid, ndev);
	rc = ib_device_set_netdev(ibdev, ndev, 1);
	/* ionic_lif_netdev() returns ndev with refcount held */
	dev_put(ndev);
	if (rc)
		goto err_admin;

	ib_set_device_ops(&dev->ibdev, &ionic_dev_ops);

	ionic_stats_init(dev);

	rc = ib_register_device(ibdev, "ionic_%d", ibdev->dev.parent);
	if (rc)
		goto err_register;

	return dev;

err_register:
	ionic_stats_cleanup(dev);
err_admin:
	ionic_kill_rdma_admin(dev, false);
	ionic_destroy_rdma_admin(dev);
err_reset:
	ionic_destroy_resids(dev);
	xa_destroy(&dev->qp_tbl);
	xa_destroy(&dev->cq_tbl);
	ib_dealloc_device(&dev->ibdev);

	return ERR_PTR(rc);
}

static int ionic_aux_probe(struct auxiliary_device *adev,
			   const struct auxiliary_device_id *id)
{
	struct ionic_aux_dev *ionic_adev;
	struct ionic_ibdev *dev;

	ionic_adev = container_of(adev, struct ionic_aux_dev, adev);
	dev = ionic_create_ibdev(ionic_adev);
	if (IS_ERR(dev))
		return dev_err_probe(&adev->dev, PTR_ERR(dev),
				     "Failed to register ibdev\n");

	auxiliary_set_drvdata(adev, dev);
	ibdev_dbg(&dev->ibdev, "registered\n");

	return 0;
}

static void ionic_aux_remove(struct auxiliary_device *adev)
{
	struct ionic_ibdev *dev = auxiliary_get_drvdata(adev);

	dev_dbg(&adev->dev, "unregister ibdev\n");
	ionic_destroy_ibdev(dev);
	dev_dbg(&adev->dev, "unregistered\n");
}

static const struct auxiliary_device_id ionic_aux_id_table[] = {
	{ .name = "ionic.rdma", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, ionic_aux_id_table);

static struct auxiliary_driver ionic_aux_r_driver = {
	.name = "rdma",
	.probe = ionic_aux_probe,
	.remove = ionic_aux_remove,
	.id_table = ionic_aux_id_table,
};

static int __init ionic_mod_init(void)
{
	int rc;

	ionic_evt_workq = create_workqueue(KBUILD_MODNAME "-evt");
	if (!ionic_evt_workq)
		return -ENOMEM;

	rc = auxiliary_driver_register(&ionic_aux_r_driver);
	if (rc)
		goto err_aux;

	return 0;

err_aux:
	destroy_workqueue(ionic_evt_workq);

	return rc;
}

static void __exit ionic_mod_exit(void)
{
	auxiliary_driver_unregister(&ionic_aux_r_driver);
	destroy_workqueue(ionic_evt_workq);
}

module_init(ionic_mod_init);
module_exit(ionic_mod_exit);
