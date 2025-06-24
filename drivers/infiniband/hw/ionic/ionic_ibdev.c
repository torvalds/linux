// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#include <linux/module.h>
#include <linux/printk.h>
#include <net/addrconf.h>

#include "ionic_ibdev.h"

#define DRIVER_DESCRIPTION "AMD Pensando RoCE HCA driver"
#define DEVICE_DESCRIPTION "AMD Pensando RoCE HCA"

MODULE_AUTHOR("Allen Hubbe <allen.hubbe@amd.com>");
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("NET_IONIC");

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
	ionic_destroy_rdma_admin(dev);
	ionic_destroy_resids(dev);
	WARN_ON(!xa_empty(&dev->cq_tbl));
	xa_destroy(&dev->cq_tbl);
	ib_dealloc_device(&dev->ibdev);
}

static struct ionic_ibdev *ionic_create_ibdev(struct ionic_aux_dev *ionic_adev)
{
	struct ib_device *ibdev;
	struct ionic_ibdev *dev;
	int rc;

	rc = ionic_version_check(&ionic_adev->adev.dev, ionic_adev->lif);
	if (rc)
		return ERR_PTR(rc);

	dev = ib_alloc_device(ionic_ibdev, ibdev);
	if (!dev)
		return ERR_PTR(-EINVAL);

	ionic_fill_lif_cfg(ionic_adev->lif, &dev->lif_cfg);

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

	addrconf_ifid_eui48((u8 *)&ibdev->node_guid,
			    ionic_lif_netdev(ionic_adev->lif));

	rc = ib_device_set_netdev(ibdev, ionic_lif_netdev(ionic_adev->lif), 1);
	if (rc)
		goto err_admin;

	rc = ib_register_device(ibdev, "ionic_%d", ibdev->dev.parent);
	if (rc)
		goto err_register;

	return dev;

err_register:
err_admin:
	ionic_kill_rdma_admin(dev, false);
	ionic_destroy_rdma_admin(dev);
err_reset:
	ionic_destroy_resids(dev);
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

	ionic_evt_workq = create_workqueue(DRIVER_NAME "-evt");
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
