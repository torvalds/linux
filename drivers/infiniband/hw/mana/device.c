// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Microsoft Corporation. All rights reserved.
 */

#include "mana_ib.h"
#include <net/mana/mana_auxiliary.h>
#include <net/addrconf.h>

MODULE_DESCRIPTION("Microsoft Azure Network Adapter IB driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("NET_MANA");

static const struct ib_device_ops mana_ib_dev_ops = {
	.owner = THIS_MODULE,
	.driver_id = RDMA_DRIVER_MANA,
	.uverbs_abi_ver = MANA_IB_UVERBS_ABI_VERSION,

	.add_gid = mana_ib_gd_add_gid,
	.alloc_pd = mana_ib_alloc_pd,
	.alloc_ucontext = mana_ib_alloc_ucontext,
	.create_ah = mana_ib_create_ah,
	.create_cq = mana_ib_create_cq,
	.create_qp = mana_ib_create_qp,
	.create_rwq_ind_table = mana_ib_create_rwq_ind_table,
	.create_wq = mana_ib_create_wq,
	.dealloc_pd = mana_ib_dealloc_pd,
	.dealloc_ucontext = mana_ib_dealloc_ucontext,
	.del_gid = mana_ib_gd_del_gid,
	.dereg_mr = mana_ib_dereg_mr,
	.destroy_ah = mana_ib_destroy_ah,
	.destroy_cq = mana_ib_destroy_cq,
	.destroy_qp = mana_ib_destroy_qp,
	.destroy_rwq_ind_table = mana_ib_destroy_rwq_ind_table,
	.destroy_wq = mana_ib_destroy_wq,
	.disassociate_ucontext = mana_ib_disassociate_ucontext,
	.get_dma_mr = mana_ib_get_dma_mr,
	.get_link_layer = mana_ib_get_link_layer,
	.get_port_immutable = mana_ib_get_port_immutable,
	.mmap = mana_ib_mmap,
	.modify_qp = mana_ib_modify_qp,
	.modify_wq = mana_ib_modify_wq,
	.poll_cq = mana_ib_poll_cq,
	.post_recv = mana_ib_post_recv,
	.post_send = mana_ib_post_send,
	.query_device = mana_ib_query_device,
	.query_gid = mana_ib_query_gid,
	.query_pkey = mana_ib_query_pkey,
	.query_port = mana_ib_query_port,
	.reg_user_mr = mana_ib_reg_user_mr,
	.reg_user_mr_dmabuf = mana_ib_reg_user_mr_dmabuf,
	.req_notify_cq = mana_ib_arm_cq,

	INIT_RDMA_OBJ_SIZE(ib_ah, mana_ib_ah, ibah),
	INIT_RDMA_OBJ_SIZE(ib_cq, mana_ib_cq, ibcq),
	INIT_RDMA_OBJ_SIZE(ib_pd, mana_ib_pd, ibpd),
	INIT_RDMA_OBJ_SIZE(ib_qp, mana_ib_qp, ibqp),
	INIT_RDMA_OBJ_SIZE(ib_ucontext, mana_ib_ucontext, ibucontext),
	INIT_RDMA_OBJ_SIZE(ib_rwq_ind_table, mana_ib_rwq_ind_table,
			   ib_ind_table),
};

static const struct ib_device_ops mana_ib_stats_ops = {
	.alloc_hw_port_stats = mana_ib_alloc_hw_port_stats,
	.get_hw_stats = mana_ib_get_hw_stats,
};

static int mana_ib_netdev_event(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct mana_ib_dev *dev = container_of(this, struct mana_ib_dev, nb);
	struct net_device *event_dev = netdev_notifier_info_to_dev(ptr);
	struct gdma_context *gc = dev->gdma_dev->gdma_context;
	struct mana_context *mc = gc->mana.driver_data;
	struct net_device *ndev;

	/* Only process events from our parent device */
	if (event_dev != mc->ports[0])
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		ndev = mana_get_primary_netdev(mc, 0, &dev->dev_tracker);
		/*
		 * RDMA core will setup GID based on updated netdev.
		 * It's not possible to race with the core as rtnl lock is being
		 * held.
		 */
		ib_device_set_netdev(&dev->ib_dev, ndev, 1);

		/* mana_get_primary_netdev() returns ndev with refcount held */
		netdev_put(ndev, &dev->dev_tracker);

		return NOTIFY_OK;
	default:
		return NOTIFY_DONE;
	}
}

static int mana_ib_probe(struct auxiliary_device *adev,
			 const struct auxiliary_device_id *id)
{
	struct mana_adev *madev = container_of(adev, struct mana_adev, adev);
	struct gdma_dev *mdev = madev->mdev;
	struct net_device *ndev;
	struct mana_context *mc;
	struct mana_ib_dev *dev;
	u8 mac_addr[ETH_ALEN];
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
	dev->ib_dev.num_comp_vectors = mdev->gdma_context->max_num_queues;
	dev->ib_dev.dev.parent = mdev->gdma_context->dev;

	ndev = mana_get_primary_netdev(mc, 0, &dev->dev_tracker);
	if (!ndev) {
		ret = -ENODEV;
		ibdev_err(&dev->ib_dev, "Failed to get netdev for IB port 1");
		goto free_ib_device;
	}
	ether_addr_copy(mac_addr, ndev->dev_addr);
	addrconf_addr_eui48((u8 *)&dev->ib_dev.node_guid, ndev->dev_addr);
	ret = ib_device_set_netdev(&dev->ib_dev, ndev, 1);
	/* mana_get_primary_netdev() returns ndev with refcount held */
	netdev_put(ndev, &dev->dev_tracker);
	if (ret) {
		ibdev_err(&dev->ib_dev, "Failed to set ib netdev, ret %d", ret);
		goto free_ib_device;
	}

	ret = mana_gd_register_device(&mdev->gdma_context->mana_ib);
	if (ret) {
		ibdev_err(&dev->ib_dev, "Failed to register device, ret %d",
			  ret);
		goto free_ib_device;
	}
	dev->gdma_dev = &mdev->gdma_context->mana_ib;

	dev->nb.notifier_call = mana_ib_netdev_event;
	ret = register_netdevice_notifier(&dev->nb);
	if (ret) {
		ibdev_err(&dev->ib_dev, "Failed to register net notifier, %d",
			  ret);
		goto deregister_device;
	}

	ret = mana_ib_gd_query_adapter_caps(dev);
	if (ret) {
		ibdev_err(&dev->ib_dev, "Failed to query device caps, ret %d",
			  ret);
		goto deregister_net_notifier;
	}

	ib_set_device_ops(&dev->ib_dev, &mana_ib_stats_ops);

	ret = mana_ib_create_eqs(dev);
	if (ret) {
		ibdev_err(&dev->ib_dev, "Failed to create EQs, ret %d", ret);
		goto deregister_net_notifier;
	}

	ret = mana_ib_gd_create_rnic_adapter(dev);
	if (ret)
		goto destroy_eqs;

	xa_init_flags(&dev->qp_table_wq, XA_FLAGS_LOCK_IRQ);
	ret = mana_ib_gd_config_mac(dev, ADDR_OP_ADD, mac_addr);
	if (ret) {
		ibdev_err(&dev->ib_dev, "Failed to add Mac address, ret %d",
			  ret);
		goto destroy_rnic;
	}

	dev->av_pool = dma_pool_create("mana_ib_av", mdev->gdma_context->dev,
				       MANA_AV_BUFFER_SIZE, MANA_AV_BUFFER_SIZE, 0);
	if (!dev->av_pool) {
		ret = -ENOMEM;
		goto destroy_rnic;
	}

	ret = ib_register_device(&dev->ib_dev, "mana_%d",
				 mdev->gdma_context->dev);
	if (ret)
		goto deallocate_pool;

	dev_set_drvdata(&adev->dev, dev);

	return 0;

deallocate_pool:
	dma_pool_destroy(dev->av_pool);
destroy_rnic:
	xa_destroy(&dev->qp_table_wq);
	mana_ib_gd_destroy_rnic_adapter(dev);
destroy_eqs:
	mana_ib_destroy_eqs(dev);
deregister_net_notifier:
	unregister_netdevice_notifier(&dev->nb);
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
	dma_pool_destroy(dev->av_pool);
	xa_destroy(&dev->qp_table_wq);
	mana_ib_gd_destroy_rnic_adapter(dev);
	mana_ib_destroy_eqs(dev);
	unregister_netdevice_notifier(&dev->nb);
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
