// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/delay.h>
#include <linux/iopoll.h>

#include "hinic3_hw_cfg.h"
#include "hinic3_hwdev.h"
#include "hinic3_lld.h"
#include "hinic3_mgmt.h"
#include "hinic3_pci_id_tbl.h"

#define HINIC3_VF_PCI_CFG_REG_BAR  0
#define HINIC3_PCI_INTR_REG_BAR    2
#define HINIC3_PCI_DB_BAR          4

#define HINIC3_EVENT_POLL_SLEEP_US   1000
#define HINIC3_EVENT_POLL_TIMEOUT_US 10000000

static struct hinic3_adev_device {
	const char *name;
} hinic3_adev_devices[HINIC3_SERVICE_T_MAX] = {
	[HINIC3_SERVICE_T_NIC] = {
		.name = "nic",
	},
};

static bool hinic3_adev_svc_supported(struct hinic3_hwdev *hwdev,
				      enum hinic3_service_type svc_type)
{
	switch (svc_type) {
	case HINIC3_SERVICE_T_NIC:
		return hinic3_support_nic(hwdev);
	default:
		break;
	}

	return false;
}

static void hinic3_comm_adev_release(struct device *dev)
{
	struct hinic3_adev *hadev = container_of(dev, struct hinic3_adev,
						 adev.dev);

	kfree(hadev);
}

static struct hinic3_adev *hinic3_add_one_adev(struct hinic3_hwdev *hwdev,
					       enum hinic3_service_type svc_type)
{
	struct hinic3_adev *hadev;
	const char *svc_name;
	int ret;

	hadev = kzalloc(sizeof(*hadev), GFP_KERNEL);
	if (!hadev)
		return NULL;

	svc_name = hinic3_adev_devices[svc_type].name;
	hadev->adev.name = svc_name;
	hadev->adev.id = hwdev->dev_id;
	hadev->adev.dev.parent = hwdev->dev;
	hadev->adev.dev.release = hinic3_comm_adev_release;
	hadev->svc_type = svc_type;
	hadev->hwdev = hwdev;

	ret = auxiliary_device_init(&hadev->adev);
	if (ret) {
		dev_err(hwdev->dev, "failed init adev %s %u\n",
			svc_name, hwdev->dev_id);
		kfree(hadev);
		return NULL;
	}

	ret = auxiliary_device_add(&hadev->adev);
	if (ret) {
		dev_err(hwdev->dev, "failed to add adev %s %u\n",
			svc_name, hwdev->dev_id);
		auxiliary_device_uninit(&hadev->adev);
		return NULL;
	}

	return hadev;
}

static void hinic3_del_one_adev(struct hinic3_hwdev *hwdev,
				enum hinic3_service_type svc_type)
{
	struct hinic3_pcidev *pci_adapter = hwdev->adapter;
	struct hinic3_adev *hadev;
	int timeout;
	bool state;

	timeout = read_poll_timeout(test_and_set_bit, state, !state,
				    HINIC3_EVENT_POLL_SLEEP_US,
				    HINIC3_EVENT_POLL_TIMEOUT_US,
				    false, svc_type, &pci_adapter->state);

	hadev = pci_adapter->hadev[svc_type];
	auxiliary_device_delete(&hadev->adev);
	auxiliary_device_uninit(&hadev->adev);
	pci_adapter->hadev[svc_type] = NULL;
	if (!timeout)
		clear_bit(svc_type, &pci_adapter->state);
}

static int hinic3_attach_aux_devices(struct hinic3_hwdev *hwdev)
{
	struct hinic3_pcidev *pci_adapter = hwdev->adapter;
	enum hinic3_service_type svc_type;

	mutex_lock(&pci_adapter->pdev_mutex);

	for (svc_type = 0; svc_type < HINIC3_SERVICE_T_MAX; svc_type++) {
		if (!hinic3_adev_svc_supported(hwdev, svc_type))
			continue;

		pci_adapter->hadev[svc_type] = hinic3_add_one_adev(hwdev,
								   svc_type);
		if (!pci_adapter->hadev[svc_type])
			goto err_del_adevs;
	}
	mutex_unlock(&pci_adapter->pdev_mutex);

	return 0;

err_del_adevs:
	while (svc_type > 0) {
		svc_type--;
		if (pci_adapter->hadev[svc_type]) {
			hinic3_del_one_adev(hwdev, svc_type);
			pci_adapter->hadev[svc_type] = NULL;
		}
	}
	mutex_unlock(&pci_adapter->pdev_mutex);

	return -ENOMEM;
}

static void hinic3_detach_aux_devices(struct hinic3_hwdev *hwdev)
{
	struct hinic3_pcidev *pci_adapter = hwdev->adapter;
	int i;

	mutex_lock(&pci_adapter->pdev_mutex);
	for (i = 0; i < ARRAY_SIZE(hinic3_adev_devices); i++) {
		if (pci_adapter->hadev[i])
			hinic3_del_one_adev(hwdev, i);
	}
	mutex_unlock(&pci_adapter->pdev_mutex);
}

struct hinic3_hwdev *hinic3_adev_get_hwdev(struct auxiliary_device *adev)
{
	struct hinic3_adev *hadev;

	hadev = container_of(adev, struct hinic3_adev, adev);

	return hadev->hwdev;
}

void hinic3_adev_event_register(struct auxiliary_device *adev,
				void (*event_handler)(struct auxiliary_device *adev,
						      struct hinic3_event_info *event))
{
	struct hinic3_adev *hadev;

	hadev = container_of(adev, struct hinic3_adev, adev);
	hadev->event = event_handler;
}

void hinic3_adev_event_unregister(struct auxiliary_device *adev)
{
	struct hinic3_adev *hadev;

	hadev = container_of(adev, struct hinic3_adev, adev);
	hadev->event = NULL;
}

static int hinic3_mapping_bar(struct pci_dev *pdev,
			      struct hinic3_pcidev *pci_adapter)
{
	pci_adapter->cfg_reg_base = pci_ioremap_bar(pdev,
						    HINIC3_VF_PCI_CFG_REG_BAR);
	if (!pci_adapter->cfg_reg_base) {
		dev_err(&pdev->dev, "Failed to map configuration regs\n");
		return -ENOMEM;
	}

	pci_adapter->intr_reg_base = pci_ioremap_bar(pdev,
						     HINIC3_PCI_INTR_REG_BAR);
	if (!pci_adapter->intr_reg_base) {
		dev_err(&pdev->dev, "Failed to map interrupt regs\n");
		goto err_unmap_cfg_reg_base;
	}

	pci_adapter->db_base_phy = pci_resource_start(pdev, HINIC3_PCI_DB_BAR);
	pci_adapter->db_dwqe_len = pci_resource_len(pdev, HINIC3_PCI_DB_BAR);
	pci_adapter->db_base = pci_ioremap_bar(pdev, HINIC3_PCI_DB_BAR);
	if (!pci_adapter->db_base) {
		dev_err(&pdev->dev, "Failed to map doorbell regs\n");
		goto err_unmap_intr_reg_base;
	}

	return 0;

err_unmap_intr_reg_base:
	iounmap(pci_adapter->intr_reg_base);

err_unmap_cfg_reg_base:
	iounmap(pci_adapter->cfg_reg_base);

	return -ENOMEM;
}

static void hinic3_unmapping_bar(struct hinic3_pcidev *pci_adapter)
{
	iounmap(pci_adapter->db_base);
	iounmap(pci_adapter->intr_reg_base);
	iounmap(pci_adapter->cfg_reg_base);
}

static int hinic3_pci_init(struct pci_dev *pdev)
{
	struct hinic3_pcidev *pci_adapter;
	int err;

	pci_adapter = kzalloc(sizeof(*pci_adapter), GFP_KERNEL);
	if (!pci_adapter)
		return -ENOMEM;

	pci_adapter->pdev = pdev;
	mutex_init(&pci_adapter->pdev_mutex);

	pci_set_drvdata(pdev, pci_adapter);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable PCI device\n");
		goto err_free_pci_adapter;
	}

	err = pci_request_regions(pdev, HINIC3_NIC_DRV_NAME);
	if (err) {
		dev_err(&pdev->dev, "Failed to request regions\n");
		goto err_disable_device;
	}

	pci_set_master(pdev);

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (err) {
		dev_err(&pdev->dev, "Failed to set DMA mask\n");
		goto err_release_regions;
	}

	return 0;

err_release_regions:
	pci_clear_master(pdev);
	pci_release_regions(pdev);

err_disable_device:
	pci_disable_device(pdev);

err_free_pci_adapter:
	pci_set_drvdata(pdev, NULL);
	mutex_destroy(&pci_adapter->pdev_mutex);
	kfree(pci_adapter);

	return err;
}

static void hinic3_pci_uninit(struct pci_dev *pdev)
{
	struct hinic3_pcidev *pci_adapter = pci_get_drvdata(pdev);

	pci_clear_master(pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	mutex_destroy(&pci_adapter->pdev_mutex);
	kfree(pci_adapter);
}

static int hinic3_func_init(struct pci_dev *pdev,
			    struct hinic3_pcidev *pci_adapter)
{
	int err;

	err = hinic3_init_hwdev(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize hardware device\n");
		return err;
	}

	err = hinic3_attach_aux_devices(pci_adapter->hwdev);
	if (err)
		goto err_free_hwdev;

	return 0;

err_free_hwdev:
	hinic3_free_hwdev(pci_adapter->hwdev);

	return err;
}

static void hinic3_func_uninit(struct pci_dev *pdev)
{
	struct hinic3_pcidev *pci_adapter = pci_get_drvdata(pdev);

	hinic3_flush_mgmt_workq(pci_adapter->hwdev);
	hinic3_detach_aux_devices(pci_adapter->hwdev);
	hinic3_free_hwdev(pci_adapter->hwdev);
}

static int hinic3_probe_func(struct hinic3_pcidev *pci_adapter)
{
	struct pci_dev *pdev = pci_adapter->pdev;
	int err;

	err = hinic3_mapping_bar(pdev, pci_adapter);
	if (err) {
		dev_err(&pdev->dev, "Failed to map bar\n");
		goto err_out;
	}

	err = hinic3_func_init(pdev, pci_adapter);
	if (err)
		goto err_unmap_bar;

	return 0;

err_unmap_bar:
	hinic3_unmapping_bar(pci_adapter);

err_out:
	dev_err(&pdev->dev, "PCIe device probe function failed\n");

	return err;
}

static void hinic3_remove_func(struct hinic3_pcidev *pci_adapter)
{
	struct pci_dev *pdev = pci_adapter->pdev;

	hinic3_func_uninit(pdev);
	hinic3_unmapping_bar(pci_adapter);
}

static int hinic3_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct hinic3_pcidev *pci_adapter;
	int err;

	err = hinic3_pci_init(pdev);
	if (err)
		goto err_out;

	pci_adapter = pci_get_drvdata(pdev);
	err = hinic3_probe_func(pci_adapter);
	if (err)
		goto err_uninit_pci;

	return 0;

err_uninit_pci:
	hinic3_pci_uninit(pdev);

err_out:
	dev_err(&pdev->dev, "PCIe device probe failed\n");

	return err;
}

static void hinic3_remove(struct pci_dev *pdev)
{
	struct hinic3_pcidev *pci_adapter = pci_get_drvdata(pdev);

	hinic3_remove_func(pci_adapter);
	hinic3_pci_uninit(pdev);
}

static const struct pci_device_id hinic3_pci_table[] = {
	{PCI_VDEVICE(HUAWEI, PCI_DEV_ID_HINIC3_VF), 0},
	{0, 0}

};

MODULE_DEVICE_TABLE(pci, hinic3_pci_table);

static void hinic3_shutdown(struct pci_dev *pdev)
{
	struct hinic3_pcidev *pci_adapter = pci_get_drvdata(pdev);

	pci_disable_device(pdev);

	if (pci_adapter)
		hinic3_set_api_stop(pci_adapter->hwdev);
}

static struct pci_driver hinic3_driver = {
	.name            = HINIC3_NIC_DRV_NAME,
	.id_table        = hinic3_pci_table,
	.probe           = hinic3_probe,
	.remove          = hinic3_remove,
	.shutdown        = hinic3_shutdown,
	.sriov_configure = pci_sriov_configure_simple
};

int hinic3_lld_init(void)
{
	return pci_register_driver(&hinic3_driver);
}

void hinic3_lld_exit(void)
{
	pci_unregister_driver(&hinic3_driver);
}
