// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2020 Intel Corporation */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <adf_accel_devices.h>
#include <adf_cfg.h>
#include <adf_common_drv.h>
#include <adf_dbgfs.h>
#include <adf_gen4_config.h>
#include <adf_gen4_hw_data.h>

#include "adf_4xxx_hw_data.h"

static const struct pci_device_id adf_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_QAT_4XXX) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_QAT_401XX) },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_INTEL_QAT_402XX) },
	{ }
};
MODULE_DEVICE_TABLE(pci, adf_pci_tbl);

static void adf_cleanup_accel(struct adf_accel_dev *accel_dev)
{
	if (accel_dev->hw_device) {
		adf_clean_hw_data_4xxx(accel_dev->hw_device);
		accel_dev->hw_device = NULL;
	}
	adf_dbgfs_exit(accel_dev);
	adf_cfg_dev_remove(accel_dev);
	adf_devmgr_rm_dev(accel_dev, NULL);
}

static int adf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct adf_accel_dev *accel_dev;
	struct adf_accel_pci *accel_pci_dev;
	struct adf_hw_device_data *hw_data;
	unsigned int i, bar_nr;
	unsigned long bar_mask;
	struct adf_bar *bar;
	int ret;

	if (num_possible_nodes() > 1 && dev_to_node(&pdev->dev) < 0) {
		/*
		 * If the accelerator is connected to a node with no memory
		 * there is no point in using the accelerator since the remote
		 * memory transaction will be very slow.
		 */
		dev_err(&pdev->dev, "Invalid NUMA configuration.\n");
		return -EINVAL;
	}

	accel_dev = devm_kzalloc(&pdev->dev, sizeof(*accel_dev), GFP_KERNEL);
	if (!accel_dev)
		return -ENOMEM;

	INIT_LIST_HEAD(&accel_dev->crypto_list);
	accel_pci_dev = &accel_dev->accel_pci_dev;
	accel_pci_dev->pci_dev = pdev;

	/*
	 * Add accel device to accel table
	 * This should be called before adf_cleanup_accel is called
	 */
	if (adf_devmgr_add_dev(accel_dev, NULL)) {
		dev_err(&pdev->dev, "Failed to add new accelerator device.\n");
		return -EFAULT;
	}

	accel_dev->owner = THIS_MODULE;
	/* Allocate and initialise device hardware meta-data structure */
	hw_data = devm_kzalloc(&pdev->dev, sizeof(*hw_data), GFP_KERNEL);
	if (!hw_data) {
		ret = -ENOMEM;
		goto out_err;
	}

	accel_dev->hw_device = hw_data;
	adf_init_hw_data_4xxx(accel_dev->hw_device, ent->device);

	pci_read_config_byte(pdev, PCI_REVISION_ID, &accel_pci_dev->revid);
	pci_read_config_dword(pdev, ADF_GEN4_FUSECTL4_OFFSET, &hw_data->fuses[ADF_FUSECTL4]);

	/* Get Accelerators and Accelerators Engines masks */
	hw_data->accel_mask = hw_data->get_accel_mask(hw_data);
	hw_data->ae_mask = hw_data->get_ae_mask(hw_data);
	accel_pci_dev->sku = hw_data->get_sku(hw_data);
	/* If the device has no acceleration engines then ignore it */
	if (!hw_data->accel_mask || !hw_data->ae_mask ||
	    (~hw_data->ae_mask & 0x01)) {
		dev_err(&pdev->dev, "No acceleration units found.\n");
		ret = -EFAULT;
		goto out_err;
	}

	/* Create device configuration table */
	ret = adf_cfg_dev_add(accel_dev);
	if (ret)
		goto out_err;

	/* Enable PCI device */
	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Can't enable PCI device.\n");
		goto out_err;
	}

	/* Set DMA identifier */
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(&pdev->dev, "No usable DMA configuration.\n");
		goto out_err;
	}

	ret = adf_gen4_cfg_dev_init(accel_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize configuration.\n");
		goto out_err;
	}

	/* Get accelerator capabilities mask */
	hw_data->accel_capabilities_mask = hw_data->get_accel_cap(accel_dev);
	if (!hw_data->accel_capabilities_mask) {
		dev_err(&pdev->dev, "Failed to get capabilities mask.\n");
		ret = -EINVAL;
		goto out_err;
	}

	/* Find and map all the device's BARS */
	bar_mask = pci_select_bars(pdev, IORESOURCE_MEM) & ADF_GEN4_BAR_MASK;

	ret = pcim_request_all_regions(pdev, pci_name(pdev));
	if (ret) {
		dev_err(&pdev->dev, "Failed to request PCI regions.\n");
		goto out_err;
	}

	i = 0;
	for_each_set_bit(bar_nr, &bar_mask, PCI_STD_NUM_BARS) {
		bar = &accel_pci_dev->pci_bars[i++];
		bar->virt_addr = pcim_iomap(pdev, bar_nr, 0);
		if (!bar->virt_addr) {
			dev_err(&pdev->dev, "Failed to ioremap PCI region.\n");
			ret = -ENOMEM;
			goto out_err;
		}
	}

	pci_set_master(pdev);

	if (pci_save_state(pdev)) {
		dev_err(&pdev->dev, "Failed to save pci state.\n");
		ret = -ENOMEM;
		goto out_err;
	}

	accel_dev->ras_errors.enabled = true;
	adf_dbgfs_init(accel_dev);

	ret = adf_dev_up(accel_dev, true);
	if (ret)
		goto out_err_dev_stop;

	ret = adf_sysfs_init(accel_dev);
	if (ret)
		goto out_err_dev_stop;

	return ret;

out_err_dev_stop:
	adf_dev_down(accel_dev);
out_err:
	adf_cleanup_accel(accel_dev);
	return ret;
}

static void adf_remove(struct pci_dev *pdev)
{
	struct adf_accel_dev *accel_dev = adf_devmgr_pci_to_accel_dev(pdev);

	if (!accel_dev) {
		pr_err("QAT: Driver removal failed\n");
		return;
	}
	adf_dev_down(accel_dev);
	adf_cleanup_accel(accel_dev);
}

static void adf_shutdown(struct pci_dev *pdev)
{
	struct adf_accel_dev *accel_dev = adf_devmgr_pci_to_accel_dev(pdev);

	adf_dev_down(accel_dev);
}

static struct pci_driver adf_driver = {
	.id_table = adf_pci_tbl,
	.name = ADF_4XXX_DEVICE_NAME,
	.probe = adf_probe,
	.remove = adf_remove,
	.shutdown = adf_shutdown,
	.sriov_configure = adf_sriov_configure,
	.err_handler = &adf_err_handler,
};

module_pci_driver(adf_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_FIRMWARE(ADF_4XXX_FW);
MODULE_FIRMWARE(ADF_402XX_FW);
MODULE_FIRMWARE(ADF_4XXX_MMP);
MODULE_FIRMWARE(ADF_402XX_MMP);
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
MODULE_VERSION(ADF_DRV_VERSION);
MODULE_SOFTDEP("pre: crypto-intel_qat");
MODULE_IMPORT_NS("CRYPTO_QAT");
