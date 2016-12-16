/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <adf_accel_devices.h>
#include <adf_common_drv.h>
#include <adf_cfg.h>
#include "adf_c62x_hw_data.h"

#define ADF_SYSTEM_DEVICE(device_id) \
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, device_id)}

static const struct pci_device_id adf_pci_tbl[] = {
	ADF_SYSTEM_DEVICE(ADF_C62X_PCI_DEVICE_ID),
	{0,}
};
MODULE_DEVICE_TABLE(pci, adf_pci_tbl);

static int adf_probe(struct pci_dev *dev, const struct pci_device_id *ent);
static void adf_remove(struct pci_dev *dev);

static struct pci_driver adf_driver = {
	.id_table = adf_pci_tbl,
	.name = ADF_C62X_DEVICE_NAME,
	.probe = adf_probe,
	.remove = adf_remove,
	.sriov_configure = adf_sriov_configure,
};

static void adf_cleanup_pci_dev(struct adf_accel_dev *accel_dev)
{
	pci_release_regions(accel_dev->accel_pci_dev.pci_dev);
	pci_disable_device(accel_dev->accel_pci_dev.pci_dev);
}

static void adf_cleanup_accel(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	int i;

	for (i = 0; i < ADF_PCI_MAX_BARS; i++) {
		struct adf_bar *bar = &accel_pci_dev->pci_bars[i];

		if (bar->virt_addr)
			pci_iounmap(accel_pci_dev->pci_dev, bar->virt_addr);
	}

	if (accel_dev->hw_device) {
		switch (accel_pci_dev->pci_dev->device) {
		case ADF_C62X_PCI_DEVICE_ID:
			adf_clean_hw_data_c62x(accel_dev->hw_device);
			break;
		default:
			break;
		}
		kfree(accel_dev->hw_device);
		accel_dev->hw_device = NULL;
	}
	adf_cfg_dev_remove(accel_dev);
	debugfs_remove(accel_dev->debugfs_dir);
	adf_devmgr_rm_dev(accel_dev, NULL);
}

static int adf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct adf_accel_dev *accel_dev;
	struct adf_accel_pci *accel_pci_dev;
	struct adf_hw_device_data *hw_data;
	char name[ADF_DEVICE_NAME_LENGTH];
	unsigned int i, bar_nr;
	int ret, bar_mask;

	switch (ent->device) {
	case ADF_C62X_PCI_DEVICE_ID:
		break;
	default:
		dev_err(&pdev->dev, "Invalid device 0x%x.\n", ent->device);
		return -ENODEV;
	}

	if (num_possible_nodes() > 1 && dev_to_node(&pdev->dev) < 0) {
		/* If the accelerator is connected to a node with no memory
		 * there is no point in using the accelerator since the remote
		 * memory transaction will be very slow. */
		dev_err(&pdev->dev, "Invalid NUMA configuration.\n");
		return -EINVAL;
	}

	accel_dev = kzalloc_node(sizeof(*accel_dev), GFP_KERNEL,
				 dev_to_node(&pdev->dev));
	if (!accel_dev)
		return -ENOMEM;

	INIT_LIST_HEAD(&accel_dev->crypto_list);
	accel_pci_dev = &accel_dev->accel_pci_dev;
	accel_pci_dev->pci_dev = pdev;

	/* Add accel device to accel table.
	 * This should be called before adf_cleanup_accel is called */
	if (adf_devmgr_add_dev(accel_dev, NULL)) {
		dev_err(&pdev->dev, "Failed to add new accelerator device.\n");
		kfree(accel_dev);
		return -EFAULT;
	}

	accel_dev->owner = THIS_MODULE;
	/* Allocate and configure device configuration structure */
	hw_data = kzalloc_node(sizeof(*hw_data), GFP_KERNEL,
			       dev_to_node(&pdev->dev));
	if (!hw_data) {
		ret = -ENOMEM;
		goto out_err;
	}

	accel_dev->hw_device = hw_data;
	adf_init_hw_data_c62x(accel_dev->hw_device);
	pci_read_config_byte(pdev, PCI_REVISION_ID, &accel_pci_dev->revid);
	pci_read_config_dword(pdev, ADF_DEVICE_FUSECTL_OFFSET,
			      &hw_data->fuses);

	/* Get Accelerators and Accelerators Engines masks */
	hw_data->accel_mask = hw_data->get_accel_mask(hw_data->fuses);
	hw_data->ae_mask = hw_data->get_ae_mask(hw_data->fuses);
	accel_pci_dev->sku = hw_data->get_sku(hw_data);
	/* If the device has no acceleration engines then ignore it. */
	if (!hw_data->accel_mask || !hw_data->ae_mask ||
	    ((~hw_data->ae_mask) & 0x01)) {
		dev_err(&pdev->dev, "No acceleration units found");
		ret = -EFAULT;
		goto out_err;
	}

	/* Create dev top level debugfs entry */
	snprintf(name, sizeof(name), "%s%s_%02x:%02d.%02d",
		 ADF_DEVICE_NAME_PREFIX, hw_data->dev_class->name,
		 pdev->bus->number, PCI_SLOT(pdev->devfn),
		 PCI_FUNC(pdev->devfn));

	accel_dev->debugfs_dir = debugfs_create_dir(name, NULL);
	if (!accel_dev->debugfs_dir) {
		dev_err(&pdev->dev, "Could not create debugfs dir %s\n", name);
		ret = -EINVAL;
		goto out_err;
	}

	/* Create device configuration table */
	ret = adf_cfg_dev_add(accel_dev);
	if (ret)
		goto out_err;

	/* enable PCI device */
	if (pci_enable_device(pdev)) {
		ret = -EFAULT;
		goto out_err;
	}

	/* set dma identifier */
	if (pci_set_dma_mask(pdev, DMA_BIT_MASK(64))) {
		if ((pci_set_dma_mask(pdev, DMA_BIT_MASK(32)))) {
			dev_err(&pdev->dev, "No usable DMA configuration\n");
			ret = -EFAULT;
			goto out_err_disable;
		} else {
			pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		}

	} else {
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	}

	if (pci_request_regions(pdev, ADF_C62X_DEVICE_NAME)) {
		ret = -EFAULT;
		goto out_err_disable;
	}

	/* Read accelerator capabilities mask */
	pci_read_config_dword(pdev, ADF_DEVICE_LEGFUSE_OFFSET,
			      &hw_data->accel_capabilities_mask);

	/* Find and map all the device's BARS */
	i = 0;
	bar_mask = pci_select_bars(pdev, IORESOURCE_MEM);
	for_each_set_bit(bar_nr, (const unsigned long *)&bar_mask,
			 ADF_PCI_MAX_BARS * 2) {
		struct adf_bar *bar = &accel_pci_dev->pci_bars[i++];

		bar->base_addr = pci_resource_start(pdev, bar_nr);
		if (!bar->base_addr)
			break;
		bar->size = pci_resource_len(pdev, bar_nr);
		bar->virt_addr = pci_iomap(accel_pci_dev->pci_dev, bar_nr, 0);
		if (!bar->virt_addr) {
			dev_err(&pdev->dev, "Failed to map BAR %d\n", bar_nr);
			ret = -EFAULT;
			goto out_err_free_reg;
		}
	}
	pci_set_master(pdev);

	if (adf_enable_aer(accel_dev, &adf_driver)) {
		dev_err(&pdev->dev, "Failed to enable aer\n");
		ret = -EFAULT;
		goto out_err_free_reg;
	}

	if (pci_save_state(pdev)) {
		dev_err(&pdev->dev, "Failed to save pci state\n");
		ret = -ENOMEM;
		goto out_err_free_reg;
	}

	ret = qat_crypto_dev_config(accel_dev);
	if (ret)
		goto out_err_free_reg;

	ret = adf_dev_init(accel_dev);
	if (ret)
		goto out_err_dev_shutdown;

	ret = adf_dev_start(accel_dev);
	if (ret)
		goto out_err_dev_stop;

	return ret;

out_err_dev_stop:
	adf_dev_stop(accel_dev);
out_err_dev_shutdown:
	adf_dev_shutdown(accel_dev);
out_err_free_reg:
	pci_release_regions(accel_pci_dev->pci_dev);
out_err_disable:
	pci_disable_device(accel_pci_dev->pci_dev);
out_err:
	adf_cleanup_accel(accel_dev);
	kfree(accel_dev);
	return ret;
}

static void adf_remove(struct pci_dev *pdev)
{
	struct adf_accel_dev *accel_dev = adf_devmgr_pci_to_accel_dev(pdev);

	if (!accel_dev) {
		pr_err("QAT: Driver removal failed\n");
		return;
	}
	adf_dev_stop(accel_dev);
	adf_dev_shutdown(accel_dev);
	adf_disable_aer(accel_dev);
	adf_cleanup_accel(accel_dev);
	adf_cleanup_pci_dev(accel_dev);
	kfree(accel_dev);
}

static int __init adfdrv_init(void)
{
	request_module("intel_qat");

	if (pci_register_driver(&adf_driver)) {
		pr_err("QAT: Driver initialization failed\n");
		return -EFAULT;
	}
	return 0;
}

static void __exit adfdrv_release(void)
{
	pci_unregister_driver(&adf_driver);
}

module_init(adfdrv_init);
module_exit(adfdrv_release);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
MODULE_VERSION(ADF_DRV_VERSION);
