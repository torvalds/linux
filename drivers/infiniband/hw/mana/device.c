// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"
#include <net/mana/mana_auxiliary.h>

MODULE_DESCRIPTION("Microsoft Azure Network Adapter IB driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(NET_MANA);

static const struct ib_device_ops mana_ib_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_MANA,
	.uverbs_abi_ver = MANA_IB_UVERBS_ABI_VERSION,

	.alloc_pd = mana_ib_alloc_pd,
	.alloc_ucontext = mana_ib_alloc_ucontext,
	.create_cq = mana_ib_create_cq,
	.create_qp = mana_ib_create_qp,
	.create_rwq_ind_table = mana_ib_create_rwq_ind_table,
	.create_wq = mana_ib_create_wq,
	.dealloc_pd = mana_ib_dealloc_pd,
	.dealloc_ucontext = mana_ib_dealloc_ucontext,
	.dereg_mr = mana_ib_dereg_mr,
	.destroy_cq = mana_ib_destroy_cq,
	.destroy_qp = mana_ib_destroy_qp,
	.destroy_rwq_ind_table = mana_ib_destroy_rwq_ind_table,
	.destroy_wq = mana_ib_destroy_wq,
	.disassociate_ucontext = mana_ib_disassociate_ucontext,
	.get_port_immutable = mana_ib_get_port_immutable,
	.mmap = mana_ib_mmap,
	.modify_qp = mana_ib_modify_qp,
	.modify_wq = mana_ib_modify_wq,
	.query_device = mana_ib_query_device,
	.query_gid = mana_ib_query_gid,
	.query_port = mana_ib_query_port,
	.reg_user_mr = mana_ib_reg_user_mr,

	INIT_RDMA_OBJ_SIZE(ib_cq, mana_ib_cq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_pd, mana_ib_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_qp, mana_ib_qp, ibqp),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, mana_ib_ucontext, ibucontext),
	INIT_RDMA_OBJ_SIZE(ib_rwq_ind_table, mana_ib_rwq_ind_table,
			   ib_ind_table),
};

static int mana_ib_probe(struct auxiliary_device *adev,
			 const struct auxiliary_device_id *id)
{
	struct mana_adev *madev = container_of(adev, struct mana_adev, adev);
	struct gdma_dev *mdev = madev->mdev;
	struct mana_context *mc;
	struct mana_ib_dev *dev;
	int ret;

	mc = mdev->driver_data;

	dev = ib_alloc_device(mana_ib_dev, ib_dev);
	if (!dev)
		return -ENOMEM;

	ib_set_device_ops(&dev->ib_dev, &mana_ib_dev_ops);

	dev->ib_dev.phys_port_cnt = mc->num_ports;

	ibdev_dbg(&dev->ib_dev, "mdev=%p id=%d num_ports=%d\n", mdev,
		  mdev->dev_id.as_uint32, dev->ib_dev.phys_port_cnt);

	dev->ib_dev.node_type = RDMA_NODE_IB_CA;

	/*
	 * num_comp_vectors needs to set to the max MSIX index
	 * when interrupts and event queues are implemented
	 */
	dev->ib_dev.num_comp_vectors = 1;
	dev->ib_dev.dev.parent = mdev->gdma_context->dev;

	ret = mana_gd_register_device(&mdev->gdma_context->mana_ib);
	if (ret) {
		ibdev_err(&dev->ib_dev, "Failed to register device, ret %d",
			  ret);
		goto free_ib_device;
	}
	dev->gdma_dev = &mdev->gdma_context->mana_ib;

	ret = mana_ib_gd_query_adapter_caps(dev);
	if (ret) {
		ibdev_err(&dev->ib_dev, "Failed to query device caps, ret %d",
			  ret);
		goto deregister_device;
	}

	ret = ib_register_device(&dev->ib_dev, "mana_%d",
				 mdev->gdma_context->dev);
	if (ret)
		goto deregister_device;

	dev_set_drvdata(&adev->dev, dev);

	return 0;

deregister_device:
	mana_gd_deregister_device(dev->gdma_dev);
free_ib_device:
	ib_dealloc_device(&dev->ib_dev);
	return ret;
}

static void mana_ib_remove(struct auxiliary_device *adev)
{
	struct mana_ib_dev *dev = dev_get_drvdata(&adev->dev);

	ib_unregister_device(&dev->ib_dev);

	mana_gd_deregister_device(dev->gdma_dev);

	ib_dealloc_device(&dev->ib_dev);
}

static const struct auxiliary_device_id mana_id_table[] = {
	{
		.name = "mana.rdma",
	},
	{},
};

MODULE_DEVICE_TABLE(auxiliary, mana_id_table);

static struct auxiliary_driver mana_driver = {
	.name = "rdma",
	.probe = mana_ib_probe,
	.remove = mana_ib_remove,
	.id_table = mana_id_table,
};

module_auxiliary_driver(mana_driver);
