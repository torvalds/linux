// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2015 - 2021 Intel Corporation */
#include "main.h"
#include <linux/net/intel/iidc_rdma_idpf.h>

MODULE_ALIAS("i40iw");
MODULE_DESCRIPTION("Intel(R) Ethernet Protocol Driver for RDMA");
MODULE_LICENSE("Dual BSD/GPL");

static struct notifier_block irdma_inetaddr_notifier = {
	.notifier_call = irdma_inetaddr_event
};

static struct notifier_block irdma_inetaddr6_notifier = {
	.notifier_call = irdma_inet6addr_event
};

static struct notifier_block irdma_net_notifier = {
	.notifier_call = irdma_net_event
};

static struct notifier_block irdma_netdevice_notifier = {
	.notifier_call = irdma_netdevice_event
};

static void irdma_register_notifiers(void)
{
	register_inetaddr_notifier(&irdma_inetaddr_notifier);
	register_inet6addr_notifier(&irdma_inetaddr6_notifier);
	register_netevent_notifier(&irdma_net_notifier);
	register_netdevice_notifier(&irdma_netdevice_notifier);
}

static void irdma_unregister_notifiers(void)
{
	unregister_netevent_notifier(&irdma_net_notifier);
	unregister_inetaddr_notifier(&irdma_inetaddr_notifier);
	unregister_inet6addr_notifier(&irdma_inetaddr6_notifier);
	unregister_netdevice_notifier(&irdma_netdevice_notifier);
}

void irdma_log_invalid_mtu(u16 mtu, struct irdma_sc_dev *dev)
{
	if (mtu < IRDMA_MIN_MTU_IPV4)
		ibdev_warn(to_ibdev(dev), "MTU setting [%d] too low for RDMA traffic. Minimum MTU is 576 for IPv4\n", mtu);
	else if (mtu < IRDMA_MIN_MTU_IPV6)
		ibdev_warn(to_ibdev(dev), "MTU setting [%d] too low for RDMA traffic. Minimum MTU is 1280 for IPv6\\n", mtu);
}

static void ig3rdma_idc_vport_event_handler(struct iidc_rdma_vport_dev_info *cdev_info,
					    struct iidc_rdma_event *event)
{
	struct irdma_device *iwdev = auxiliary_get_drvdata(cdev_info->adev);
	struct irdma_l2params l2params = {};

	if (*event->type & BIT(IIDC_RDMA_EVENT_AFTER_MTU_CHANGE)) {
		ibdev_dbg(&iwdev->ibdev, "CLNT: new MTU = %d\n", iwdev->netdev->mtu);
		if (iwdev->vsi.mtu != iwdev->netdev->mtu) {
			l2params.mtu = iwdev->netdev->mtu;
			l2params.mtu_changed = true;
			irdma_log_invalid_mtu(l2params.mtu, &iwdev->rf->sc_dev);
			irdma_change_l2params(&iwdev->vsi, &l2params);
		}
	}
}

static int ig3rdma_vport_probe(struct auxiliary_device *aux_dev,
			       const struct auxiliary_device_id *id)
{
	struct iidc_rdma_vport_auxiliary_dev *idc_adev =
		container_of(aux_dev, struct iidc_rdma_vport_auxiliary_dev, adev);
	struct auxiliary_device *aux_core_dev = idc_adev->vdev_info->core_adev;
	struct irdma_pci_f *rf = auxiliary_get_drvdata(aux_core_dev);
	struct irdma_l2params l2params = {};
	struct irdma_device *iwdev;
	int err;

	if (!rf) {
		WARN_ON_ONCE(1);
		return -ENOMEM;
	}
	iwdev = ib_alloc_device(irdma_device, ibdev);
	/* Fill iwdev info */
	iwdev->is_vport = true;
	iwdev->rf = rf;
	iwdev->vport_id = idc_adev->vdev_info->vport_id;
	iwdev->netdev = idc_adev->vdev_info->netdev;
	iwdev->init_state = INITIAL_STATE;
	iwdev->roce_cwnd = IRDMA_ROCE_CWND_DEFAULT;
	iwdev->roce_ackcreds = IRDMA_ROCE_ACKCREDS_DEFAULT;
	iwdev->rcv_wnd = IRDMA_CM_DEFAULT_RCV_WND_SCALED;
	iwdev->rcv_wscale = IRDMA_CM_DEFAULT_RCV_WND_SCALE;
	iwdev->roce_mode = true;
	iwdev->push_mode = false;

	l2params.mtu = iwdev->netdev->mtu;

	err = irdma_rt_init_hw(iwdev, &l2params);
	if (err)
		goto err_rt_init;

	err = irdma_ib_register_device(iwdev);
	if (err)
		goto err_ibreg;

	auxiliary_set_drvdata(aux_dev, iwdev);

	ibdev_dbg(&iwdev->ibdev,
		  "INIT: Gen[%d] vport[%d] probe success. dev_name = %s, core_dev_name = %s, netdev=%s\n",
		  rf->rdma_ver, idc_adev->vdev_info->vport_id,
		  dev_name(&aux_dev->dev),
		  dev_name(&idc_adev->vdev_info->core_adev->dev),
		  netdev_name(idc_adev->vdev_info->netdev));

	return 0;
err_ibreg:
	irdma_rt_deinit_hw(iwdev);
err_rt_init:
	ib_dealloc_device(&iwdev->ibdev);

	return err;
}

static void ig3rdma_vport_remove(struct auxiliary_device *aux_dev)
{
	struct iidc_rdma_vport_auxiliary_dev *idc_adev =
		container_of(aux_dev, struct iidc_rdma_vport_auxiliary_dev, adev);
	struct irdma_device *iwdev = auxiliary_get_drvdata(aux_dev);

	ibdev_dbg(&iwdev->ibdev,
		  "INIT: Gen[%d] dev_name = %s, core_dev_name = %s, netdev=%s\n",
		  iwdev->rf->rdma_ver, dev_name(&aux_dev->dev),
		  dev_name(&idc_adev->vdev_info->core_adev->dev),
		  netdev_name(idc_adev->vdev_info->netdev));

	irdma_ib_unregister_device(iwdev);
}

static const struct auxiliary_device_id ig3rdma_vport_auxiliary_id_table[] = {
	{.name = "idpf.8086.rdma.vdev", },
	{},
};

MODULE_DEVICE_TABLE(auxiliary, ig3rdma_vport_auxiliary_id_table);

static struct iidc_rdma_vport_auxiliary_drv ig3rdma_vport_auxiliary_drv = {
	.adrv = {
		.name = "vdev",
		.id_table = ig3rdma_vport_auxiliary_id_table,
		.probe = ig3rdma_vport_probe,
		.remove = ig3rdma_vport_remove,
	},
	.event_handler = ig3rdma_idc_vport_event_handler,
};


static int __init irdma_init_module(void)
{
	int ret;

	ret = auxiliary_driver_register(&i40iw_auxiliary_drv);
	if (ret) {
		pr_err("Failed i40iw(gen_1) auxiliary_driver_register() ret=%d\n",
		       ret);
		return ret;
	}

	ret = auxiliary_driver_register(&icrdma_core_auxiliary_drv.adrv);
	if (ret) {
		auxiliary_driver_unregister(&i40iw_auxiliary_drv);
		pr_err("Failed icrdma(gen_2) auxiliary_driver_register() ret=%d\n",
		       ret);
		return ret;
	}

	ret = auxiliary_driver_register(&ig3rdma_core_auxiliary_drv.adrv);
	if (ret) {
		auxiliary_driver_unregister(&icrdma_core_auxiliary_drv.adrv);
		auxiliary_driver_unregister(&i40iw_auxiliary_drv);
		pr_err("Failed ig3rdma(gen_3) core auxiliary_driver_register() ret=%d\n",
		       ret);

		return ret;
	}

	ret = auxiliary_driver_register(&ig3rdma_vport_auxiliary_drv.adrv);
	if (ret) {
		auxiliary_driver_unregister(&ig3rdma_core_auxiliary_drv.adrv);
		auxiliary_driver_unregister(&icrdma_core_auxiliary_drv.adrv);
		auxiliary_driver_unregister(&i40iw_auxiliary_drv);
		pr_err("Failed ig3rdma vport auxiliary_driver_register() ret=%d\n",
		       ret);

		return ret;
	}
	irdma_register_notifiers();

	return 0;
}

static void __exit irdma_exit_module(void)
{
	irdma_unregister_notifiers();
	auxiliary_driver_unregister(&icrdma_core_auxiliary_drv.adrv);
	auxiliary_driver_unregister(&i40iw_auxiliary_drv);
	auxiliary_driver_unregister(&ig3rdma_core_auxiliary_drv.adrv);
	auxiliary_driver_unregister(&ig3rdma_vport_auxiliary_drv.adrv);
}

module_init(irdma_init_module);
module_exit(irdma_exit_module);
