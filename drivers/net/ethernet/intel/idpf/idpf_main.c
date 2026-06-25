// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"
#include "idpf_devids.h"
#include "idpf_lan_vf_regs.h"
#include "idpf_virtchnl.h"

#define DRV_SUMMARY	"Intel(R) Infrastructure Data Path Function Linux Driver"

#define IDPF_NETWORK_ETHERNET_PROGIF				0x01
#define IDPF_CLASS_NETWORK_ETHERNET_PROGIF			\
	(PCI_CLASS_NETWORK_ETHERNET << 8 | IDPF_NETWORK_ETHERNET_PROGIF)
#define IDPF_VF_TEST_VAL		0xfeed0000u

MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_IMPORT_NS("LIBETH");
MODULE_IMPORT_NS("LIBIE_CP");
MODULE_IMPORT_NS("LIBIE_PCI");
MODULE_IMPORT_NS("LIBETH_XDP");
MODULE_LICENSE("GPL");

/**
 * idpf_get_device_type - Helper to find if it is a VF or PF device
 * @pdev: PCI device information struct
 *
 * Return: PF/VF device ID or -%errno on failure.
 */
static int idpf_get_device_type(struct pci_dev *pdev)
{
	void __iomem *addr;
	int ret;

	addr = ioremap(pci_resource_start(pdev, 0) + VF_ARQBAL, 4);
	if (!addr) {
		pci_err(pdev, "Failed to allocate BAR0 mbx region\n");
		return -EIO;
	}

	writel(IDPF_VF_TEST_VAL, addr);
	if (readl(addr) == IDPF_VF_TEST_VAL)
		ret = IDPF_DEV_ID_VF;
	else
		ret = IDPF_DEV_ID_PF;

	iounmap(addr);

	return ret;
}

/**
 * idpf_dev_init - Initialize device specific parameters
 * @adapter: adapter to initialize
 * @ent: entry in idpf_pci_tbl
 *
 * Return: %0 on success, -%errno on failure.
 */
static int idpf_dev_init(struct idpf_adapter *adapter,
			 const struct pci_device_id *ent)
{
	struct libie_mmio_info *mmio_info = &adapter->ctlq_ctx.mmio_info;
	int ret;

	ret = libie_pci_init_dev(adapter->pdev);
	if (ret)
		return ret;

	mmio_info->pdev = adapter->pdev;
	INIT_LIST_HEAD(&mmio_info->mmio_list);

	if (ent->class == IDPF_CLASS_NETWORK_ETHERNET_PROGIF) {
		ret = idpf_get_device_type(adapter->pdev);
		switch (ret) {
		case IDPF_DEV_ID_VF:
			idpf_vf_dev_ops_init(adapter);
			adapter->crc_enable = true;
			break;
		case IDPF_DEV_ID_PF:
			idpf_dev_ops_init(adapter);
			break;
		default:
			return ret;
		}

		return 0;
	}

	switch (ent->device) {
	case IDPF_DEV_ID_PF:
		idpf_dev_ops_init(adapter);
		break;
	case IDPF_DEV_ID_VF:
		idpf_vf_dev_ops_init(adapter);
		adapter->crc_enable = true;
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

/**
 * idpf_decfg_device - deconfigure device and device specific resources
 * @adapter: driver specific private structure
 */
static void idpf_decfg_device(struct idpf_adapter *adapter)
{
	libie_pci_unmap_all_mmio_regions(&adapter->ctlq_ctx.mmio_info);
}

/**
 * idpf_remove - Device removal routine
 * @pdev: PCI device information struct
 */
static void idpf_remove(struct pci_dev *pdev)
{
	struct idpf_adapter *adapter = pci_get_drvdata(pdev);
	int i;

	set_bit(IDPF_REMOVE_IN_PROG, adapter->flags);

	/* Wait until vc_event_task is done to consider if any hard reset is
	 * in progress else we may go ahead and release the resources but the
	 * thread doing the hard reset might continue the init path and
	 * end up in bad state.
	 */
	cancel_delayed_work_sync(&adapter->vc_event_task);
	if (adapter->num_vfs)
		idpf_sriov_configure(pdev, 0);

	idpf_vc_core_deinit(adapter);

	/* Be a good citizen and leave the device clean on exit */
	adapter->dev_ops.reg_ops.trigger_reset(adapter, IDPF_HR_FUNC_RESET);
	idpf_deinit_dflt_mbx(adapter);

	if (!adapter->netdevs)
		goto destroy_wqs;

	/* There are some cases where it's possible to still have netdevs
	 * registered with the stack at this point, e.g. if the driver detected
	 * a HW reset and rmmod is called before it fully recovers. Unregister
	 * any stale netdevs here.
	 */
	for (i = 0; i < adapter->max_vports; i++) {
		if (!adapter->netdevs[i])
			continue;
		if (adapter->netdevs[i]->reg_state != NETREG_UNINITIALIZED)
			unregister_netdev(adapter->netdevs[i]);
		free_netdev(adapter->netdevs[i]);
		adapter->netdevs[i] = NULL;
	}

destroy_wqs:
	destroy_workqueue(adapter->init_wq);
	destroy_workqueue(adapter->serv_wq);
	destroy_workqueue(adapter->mbx_wq);
	destroy_workqueue(adapter->stats_wq);
	destroy_workqueue(adapter->vc_event_wq);

	for (i = 0; i < adapter->max_vports; i++) {
		if (!adapter->vport_config[i])
			continue;
		kfree(adapter->vport_config[i]->user_config.q_coalesce);
		kfree(adapter->vport_config[i]);
		adapter->vport_config[i] = NULL;
	}
	kfree(adapter->vport_config);
	adapter->vport_config = NULL;
	kfree(adapter->netdevs);
	adapter->netdevs = NULL;

	mutex_destroy(&adapter->vport_ctrl_lock);
	mutex_destroy(&adapter->vector_lock);
	mutex_destroy(&adapter->queue_lock);
	mutex_destroy(&adapter->vc_buf_lock);

	idpf_decfg_device(adapter);
	pci_set_drvdata(pdev, NULL);
	kfree(adapter);
}

/**
 * idpf_shutdown - PCI callback for shutting down device
 * @pdev: PCI device information struct
 */
static void idpf_shutdown(struct pci_dev *pdev)
{
	struct idpf_adapter *adapter = pci_get_drvdata(pdev);

	cancel_delayed_work_sync(&adapter->serv_task);
	cancel_delayed_work_sync(&adapter->vc_event_task);
	idpf_vc_core_deinit(adapter);
	idpf_deinit_dflt_mbx(adapter);

	if (system_state == SYSTEM_POWER_OFF)
		pci_set_power_state(pdev, PCI_D3hot);
}

/**
 * idpf_cfg_device - configure device and device specific resources
 * @adapter: driver specific private structure
 *
 * Return: %0 on success, -%errno on failure.
 */
static int idpf_cfg_device(struct idpf_adapter *adapter)
{
	struct libie_mmio_info *mmio_info = &adapter->ctlq_ctx.mmio_info;
	struct pci_dev *pdev = adapter->pdev;
	struct resource *region;
	bool mapped;
	int err;

	/* Map mailbox space for virtchnl communication */
	region = &adapter->dev_ops.static_reg_info[0];
	mapped = libie_pci_map_mmio_region(mmio_info, region->start,
					   resource_size(region));
	if (!mapped) {
		pci_err(pdev, "failed to map BAR0 mbx region\n");
		return -ENOMEM;
	}

	/* Map rstat space for resets */
	region = &adapter->dev_ops.static_reg_info[1];

	mapped = libie_pci_map_mmio_region(mmio_info, region->start,
					   resource_size(region));
	if (!mapped) {
		pci_err(pdev, "failed to map BAR0 rstat region\n");
		libie_pci_unmap_all_mmio_regions(mmio_info);
		return -ENOMEM;
	}

	err = pci_enable_ptm(pdev);
	if (err)
		pci_dbg(pdev, "PCIe PTM is not supported by PCIe bus/controller\n");

	pci_set_drvdata(pdev, adapter);

	return 0;
}

/**
 * idpf_probe - Device initialization routine
 * @pdev: PCI device information struct
 * @ent: entry in idpf_pci_tbl
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct device *dev = &pdev->dev;
	struct idpf_adapter *adapter;
	int err;

	adapter = kzalloc_obj(*adapter);
	if (!adapter)
		return -ENOMEM;

	adapter->req_tx_splitq = true;
	adapter->req_rx_splitq = true;

	adapter->pdev = pdev;

	err = idpf_dev_init(adapter, ent);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize device (ID 0x%x): %d\n",
			ent->device, err);
		goto err_free;
	}

	err = idpf_cfg_device(adapter);
	if (err) {
		pci_err(pdev, "Failed to configure device specific resources: %pe\n",
			ERR_PTR(err));
		goto err_free;
	}

	adapter->init_wq = alloc_workqueue("%s-%s-init",
					   WQ_UNBOUND | WQ_MEM_RECLAIM, 0,
					   dev_driver_string(dev),
					   dev_name(dev));
	if (!adapter->init_wq) {
		dev_err(dev, "Failed to allocate init workqueue\n");
		err = -ENOMEM;
		goto err_init_wq;
	}

	adapter->serv_wq = alloc_workqueue("%s-%s-service",
					   WQ_UNBOUND | WQ_MEM_RECLAIM, 0,
					   dev_driver_string(dev),
					   dev_name(dev));
	if (!adapter->serv_wq) {
		dev_err(dev, "Failed to allocate service workqueue\n");
		err = -ENOMEM;
		goto err_serv_wq_alloc;
	}

	adapter->mbx_wq = alloc_workqueue("%s-%s-mbx", WQ_UNBOUND | WQ_HIGHPRI,
					  0, dev_driver_string(dev),
					  dev_name(dev));
	if (!adapter->mbx_wq) {
		dev_err(dev, "Failed to allocate mailbox workqueue\n");
		err = -ENOMEM;
		goto err_mbx_wq_alloc;
	}

	adapter->stats_wq = alloc_workqueue("%s-%s-stats",
					    WQ_UNBOUND | WQ_MEM_RECLAIM, 0,
					    dev_driver_string(dev),
					    dev_name(dev));
	if (!adapter->stats_wq) {
		dev_err(dev, "Failed to allocate workqueue\n");
		err = -ENOMEM;
		goto err_stats_wq_alloc;
	}

	adapter->vc_event_wq = alloc_workqueue("%s-%s-vc_event",
					       WQ_UNBOUND | WQ_MEM_RECLAIM, 0,
					       dev_driver_string(dev),
					       dev_name(dev));
	if (!adapter->vc_event_wq) {
		dev_err(dev, "Failed to allocate virtchnl event workqueue\n");
		err = -ENOMEM;
		goto err_vc_event_wq_alloc;
	}

	/* setup msglvl */
	adapter->msg_enable = netif_msg_init(-1, IDPF_AVAIL_NETIF_M);

	mutex_init(&adapter->vport_ctrl_lock);
	mutex_init(&adapter->vector_lock);
	mutex_init(&adapter->queue_lock);
	mutex_init(&adapter->vc_buf_lock);

	INIT_DELAYED_WORK(&adapter->init_task, idpf_init_task);
	INIT_DELAYED_WORK(&adapter->serv_task, idpf_service_task);
	INIT_DELAYED_WORK(&adapter->mbx_task, idpf_mbx_task);
	INIT_DELAYED_WORK(&adapter->stats_task, idpf_statistics_task);
	INIT_DELAYED_WORK(&adapter->vc_event_task, idpf_vc_event_task);

	adapter->dev_ops.reg_ops.reset_reg_init(adapter);
	set_bit(IDPF_HR_DRV_LOAD, adapter->flags);
	queue_delayed_work(adapter->vc_event_wq, &adapter->vc_event_task,
			   msecs_to_jiffies(10 * (pdev->devfn & 0x07)));

	return 0;

err_vc_event_wq_alloc:
	destroy_workqueue(adapter->stats_wq);
err_stats_wq_alloc:
	destroy_workqueue(adapter->mbx_wq);
err_mbx_wq_alloc:
	destroy_workqueue(adapter->serv_wq);
err_serv_wq_alloc:
	destroy_workqueue(adapter->init_wq);
err_init_wq:
	idpf_decfg_device(adapter);
err_free:
	kfree(adapter);
	return err;
}

/* idpf_pci_tbl - PCI Dev idpf ID Table
 */
static const struct pci_device_id idpf_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, IDPF_DEV_ID_PF)},
	{ PCI_VDEVICE(INTEL, IDPF_DEV_ID_VF)},
	{ PCI_DEVICE_CLASS(IDPF_CLASS_NETWORK_ETHERNET_PROGIF, ~0)},
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(pci, idpf_pci_tbl);

static struct pci_driver idpf_driver = {
	.name			= KBUILD_MODNAME,
	.id_table		= idpf_pci_tbl,
	.probe			= idpf_probe,
	.sriov_configure	= idpf_sriov_configure,
	.remove			= idpf_remove,
	.shutdown		= idpf_shutdown,
};
module_pci_driver(idpf_driver);
