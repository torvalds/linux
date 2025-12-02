// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2023 Intel Corporation */

#include "idpf.h"
#include "idpf_devids.h"
#include "idpf_virtchnl.h"

#define DRV_SUMMARY	"Intel(R) Infrastructure Data Path Function Linux Driver"

MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_IMPORT_NS("LIBETH");
MODULE_IMPORT_NS("LIBETH_XDP");
MODULE_LICENSE("GPL");

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
	kfree(adapter->vcxn_mngr);
	adapter->vcxn_mngr = NULL;

	mutex_destroy(&adapter->vport_ctrl_lock);
	mutex_destroy(&adapter->vector_lock);
	mutex_destroy(&adapter->queue_lock);
	mutex_destroy(&adapter->vc_buf_lock);

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
 * idpf_cfg_hw - Initialize HW struct
 * @adapter: adapter to setup hw struct for
 *
 * Returns 0 on success, negative on failure
 */
static int idpf_cfg_hw(struct idpf_adapter *adapter)
{
	resource_size_t res_start, mbx_start, rstat_start;
	struct pci_dev *pdev = adapter->pdev;
	struct idpf_hw *hw = &adapter->hw;
	struct device *dev = &pdev->dev;
	long len;

	res_start = pci_resource_start(pdev, 0);

	/* Map mailbox space for virtchnl communication */
	mbx_start = res_start + adapter->dev_ops.static_reg_info[0].start;
	len = resource_size(&adapter->dev_ops.static_reg_info[0]);
	hw->mbx.vaddr = devm_ioremap(dev, mbx_start, len);
	if (!hw->mbx.vaddr) {
		pci_err(pdev, "failed to allocate BAR0 mbx region\n");

		return -ENOMEM;
	}
	hw->mbx.addr_start = adapter->dev_ops.static_reg_info[0].start;
	hw->mbx.addr_len = len;

	/* Map rstat space for resets */
	rstat_start = res_start + adapter->dev_ops.static_reg_info[1].start;
	len = resource_size(&adapter->dev_ops.static_reg_info[1]);
	hw->rstat.vaddr = devm_ioremap(dev, rstat_start, len);
	if (!hw->rstat.vaddr) {
		pci_err(pdev, "failed to allocate BAR0 rstat region\n");

		return -ENOMEM;
	}
	hw->rstat.addr_start = adapter->dev_ops.static_reg_info[1].start;
	hw->rstat.addr_len = len;

	hw->back = adapter;

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

	adapter = kzalloc(sizeof(*adapter), GFP_KERNEL);
	if (!adapter)
		return -ENOMEM;

	adapter->req_tx_splitq = true;
	adapter->req_rx_splitq = true;

	switch (ent->device) {
	case IDPF_DEV_ID_PF:
		idpf_dev_ops_init(adapter);
		break;
	case IDPF_DEV_ID_VF:
		idpf_vf_dev_ops_init(adapter);
		adapter->crc_enable = true;
		break;
	default:
		err = -ENODEV;
		dev_err(&pdev->dev, "Unexpected dev ID 0x%x in idpf probe\n",
			ent->device);
		goto err_free;
	}

	adapter->pdev = pdev;
	err = pcim_enable_device(pdev);
	if (err)
		goto err_free;

	err = pcim_request_region(pdev, 0, pci_name(pdev));
	if (err) {
		pci_err(pdev, "pcim_request_region failed %pe\n", ERR_PTR(err));

		goto err_free;
	}

	err = pci_enable_ptm(pdev, NULL);
	if (err)
		pci_dbg(pdev, "PCIe PTM is not supported by PCIe bus/controller\n");

	/* set up for high or low dma */
	err = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (err) {
		pci_err(pdev, "DMA configuration failed: %pe\n", ERR_PTR(err));

		goto err_free;
	}

	pci_set_master(pdev);
	pci_set_drvdata(pdev, adapter);

	adapter->init_wq = alloc_workqueue("%s-%s-init",
					   WQ_UNBOUND | WQ_MEM_RECLAIM, 0,
					   dev_driver_string(dev),
					   dev_name(dev));
	if (!adapter->init_wq) {
		dev_err(dev, "Failed to allocate init workqueue\n");
		err = -ENOMEM;
		goto err_free;
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

	err = idpf_cfg_hw(adapter);
	if (err) {
		dev_err(dev, "Failed to configure HW structure for adapter: %d\n",
			err);
		goto err_cfg_hw;
	}

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

err_cfg_hw:
	destroy_workqueue(adapter->vc_event_wq);
err_vc_event_wq_alloc:
	destroy_workqueue(adapter->stats_wq);
err_stats_wq_alloc:
	destroy_workqueue(adapter->mbx_wq);
err_mbx_wq_alloc:
	destroy_workqueue(adapter->serv_wq);
err_serv_wq_alloc:
	destroy_workqueue(adapter->init_wq);
err_free:
	kfree(adapter);
	return err;
}

/* idpf_pci_tbl - PCI Dev idpf ID Table
 */
static const struct pci_device_id idpf_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, IDPF_DEV_ID_PF)},
	{ PCI_VDEVICE(INTEL, IDPF_DEV_ID_VF)},
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
