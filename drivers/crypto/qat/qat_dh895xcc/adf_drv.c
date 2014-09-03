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
#include <adf_transport_access_macros.h>
#include "adf_dh895xcc_hw_data.h"
#include "adf_drv.h"

static const char adf_driver_name[] = ADF_DH895XCC_DEVICE_NAME;

#define ADF_SYSTEM_DEVICE(device_id) \
	{PCI_DEVICE(PCI_VENDOR_ID_INTEL, device_id)}

static const struct pci_device_id adf_pci_tbl[] = {
	ADF_SYSTEM_DEVICE(ADF_DH895XCC_PCI_DEVICE_ID),
	{0,}
};
MODULE_DEVICE_TABLE(pci, adf_pci_tbl);

static int adf_probe(struct pci_dev *dev, const struct pci_device_id *ent);
static void adf_remove(struct pci_dev *dev);

static struct pci_driver adf_driver = {
	.id_table = adf_pci_tbl,
	.name = adf_driver_name,
	.probe = adf_probe,
	.remove = adf_remove
};

static void adf_cleanup_accel(struct adf_accel_dev *accel_dev)
{
	struct adf_accel_pci *accel_pci_dev = &accel_dev->accel_pci_dev;
	int i;

	adf_exit_admin_comms(accel_dev);
	adf_exit_arb(accel_dev);
	adf_cleanup_etr_data(accel_dev);

	for (i = 0; i < ADF_PCI_MAX_BARS; i++) {
		struct adf_bar *bar = &accel_pci_dev->pci_bars[i];

		if (bar->virt_addr)
			pci_iounmap(accel_pci_dev->pci_dev, bar->virt_addr);
	}

	if (accel_dev->hw_device) {
		switch (accel_dev->hw_device->pci_dev_id) {
		case ADF_DH895XCC_PCI_DEVICE_ID:
			adf_clean_hw_data_dh895xcc(accel_dev->hw_device);
			break;
		default:
			break;
		}
		kfree(accel_dev->hw_device);
	}
	adf_cfg_dev_remove(accel_dev);
	debugfs_remove(accel_dev->debugfs_dir);
	adf_devmgr_rm_dev(accel_dev);
	pci_release_regions(accel_pci_dev->pci_dev);
	pci_disable_device(accel_pci_dev->pci_dev);
	kfree(accel_dev);
}

static uint8_t adf_get_dev_node_id(struct pci_dev *pdev)
{
	unsigned int bus_per_cpu = 0;
	struct cpuinfo_x86 *c = &cpu_data(num_online_cpus() - 1);

	if (!c->phys_proc_id)
		return 0;

	bus_per_cpu = 256 / (c->phys_proc_id + 1);

	if (bus_per_cpu != 0)
		return pdev->bus->number / bus_per_cpu;
	return 0;
}

static int qat_dev_start(struct adf_accel_dev *accel_dev)
{
	int cpus = num_online_cpus();
	int banks = GET_MAX_BANKS(accel_dev);
	int instances = min(cpus, banks);
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	int i;
	unsigned long val;

	if (adf_cfg_section_add(accel_dev, ADF_KERNEL_SEC))
		goto err;
	if (adf_cfg_section_add(accel_dev, "Accelerator0"))
		goto err;
	for (i = 0; i < instances; i++) {
		val = i;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_BANK_NUM, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_ETRMGR_CORE_AFFINITY,
			 i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_SIZE, i);
		val = 128;
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = 512;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_SIZE, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = 0;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_TX, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = 2;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_TX, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = 4;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_RND_TX, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = 8;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_RX, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = 10;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_RX, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = 12;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_RND_RX, i);
		if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						key, (void *)&val, ADF_DEC))
			goto err;

		val = ADF_COALESCING_DEF_TIME;
		snprintf(key, sizeof(key), ADF_ETRMGR_COALESCE_TIMER_FORMAT, i);
		if (adf_cfg_add_key_value_param(accel_dev, "Accelerator0",
						key, (void *)&val, ADF_DEC))
			goto err;
	}

	val = i;
	if (adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
					ADF_NUM_CY, (void *)&val, ADF_DEC))
		goto err;

	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);
	return adf_dev_start(accel_dev);
err:
	dev_err(&GET_DEV(accel_dev), "Failed to start QAT accel dev\n");
	return -EINVAL;
}

static int adf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct adf_accel_dev *accel_dev;
	struct adf_accel_pci *accel_pci_dev;
	struct adf_hw_device_data *hw_data;
	void __iomem *pmisc_bar_addr = NULL;
	char name[ADF_DEVICE_NAME_LENGTH];
	unsigned int i, bar_nr;
	uint8_t node;
	int ret;

	switch (ent->device) {
	case ADF_DH895XCC_PCI_DEVICE_ID:
		break;
	default:
		dev_err(&pdev->dev, "Invalid device 0x%x.\n", ent->device);
		return -ENODEV;
	}

	node = adf_get_dev_node_id(pdev);
	accel_dev = kzalloc_node(sizeof(*accel_dev), GFP_KERNEL, node);
	if (!accel_dev)
		return -ENOMEM;

	accel_dev->numa_node = node;
	INIT_LIST_HEAD(&accel_dev->crypto_list);

	/* Add accel device to accel table.
	 * This should be called before adf_cleanup_accel is called */
	if (adf_devmgr_add_dev(accel_dev)) {
		dev_err(&pdev->dev, "Failed to add new accelerator device.\n");
		kfree(accel_dev);
		return -EFAULT;
	}

	accel_dev->owner = THIS_MODULE;
	/* Allocate and configure device configuration structure */
	hw_data = kzalloc_node(sizeof(*hw_data), GFP_KERNEL, node);
	if (!hw_data) {
		ret = -ENOMEM;
		goto out_err;
	}

	accel_dev->hw_device = hw_data;
	switch (ent->device) {
	case ADF_DH895XCC_PCI_DEVICE_ID:
		adf_init_hw_data_dh895xcc(accel_dev->hw_device);
		break;
	default:
		return -ENODEV;
	}
	accel_pci_dev = &accel_dev->accel_pci_dev;
	pci_read_config_byte(pdev, PCI_REVISION_ID, &accel_pci_dev->revid);
	pci_read_config_dword(pdev, ADF_DH895XCC_FUSECTL_OFFSET,
			      &hw_data->fuses);

	/* Get Accelerators and Accelerators Engines masks */
	hw_data->accel_mask = hw_data->get_accel_mask(hw_data->fuses);
	hw_data->ae_mask = hw_data->get_ae_mask(hw_data->fuses);
	accel_pci_dev->sku = hw_data->get_sku(hw_data);
	accel_pci_dev->pci_dev = pdev;
	/* If the device has no acceleration engines then ignore it. */
	if (!hw_data->accel_mask || !hw_data->ae_mask ||
	    ((~hw_data->ae_mask) & 0x01)) {
		dev_err(&pdev->dev, "No acceleration units found");
		ret = -EFAULT;
		goto out_err;
	}

	/* Create dev top level debugfs entry */
	snprintf(name, sizeof(name), "%s%s_dev%d", ADF_DEVICE_NAME_PREFIX,
		 hw_data->dev_class->name, hw_data->instance_id);
	accel_dev->debugfs_dir = debugfs_create_dir(name, NULL);
	if (!accel_dev->debugfs_dir) {
		dev_err(&pdev->dev, "Could not create debugfs dir\n");
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
			goto out_err;
		} else {
			pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
		}

	} else {
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	}

	if (pci_request_regions(pdev, adf_driver_name)) {
		ret = -EFAULT;
		goto out_err;
	}

	/* Read accelerator capabilities mask */
	pci_read_config_dword(pdev, ADF_DH895XCC_LEGFUSE_OFFSET,
			      &hw_data->accel_capabilities_mask);

	/* Find and map all the device's BARS */
	for (i = 0; i < ADF_PCI_MAX_BARS; i++) {
		struct adf_bar *bar = &accel_pci_dev->pci_bars[i];

		bar_nr = i * 2;
		bar->base_addr = pci_resource_start(pdev, bar_nr);
		if (!bar->base_addr)
			break;
		bar->size = pci_resource_len(pdev, bar_nr);
		bar->virt_addr = pci_iomap(accel_pci_dev->pci_dev, bar_nr, 0);
		if (!bar->virt_addr) {
			dev_err(&pdev->dev, "Failed to map BAR %d\n", i);
			ret = -EFAULT;
			goto out_err;
		}
		if (i == ADF_DH895XCC_PMISC_BAR)
			pmisc_bar_addr = bar->virt_addr;
	}
	pci_set_master(pdev);

	if (adf_enable_aer(accel_dev, &adf_driver)) {
		dev_err(&pdev->dev, "Failed to enable aer\n");
		ret = -EFAULT;
		goto out_err;
	}

	if (adf_init_etr_data(accel_dev)) {
		dev_err(&pdev->dev, "Failed initialize etr\n");
		ret = -EFAULT;
		goto out_err;
	}

	if (adf_init_admin_comms(accel_dev)) {
		dev_err(&pdev->dev, "Failed initialize admin comms\n");
		ret = -EFAULT;
		goto out_err;
	}

	if (adf_init_arb(accel_dev)) {
		dev_err(&pdev->dev, "Failed initialize hw arbiter\n");
		ret = -EFAULT;
		goto out_err;
	}
	if (pci_save_state(pdev)) {
		dev_err(&pdev->dev, "Failed to save pci state\n");
		ret = -ENOMEM;
		goto out_err;
	}

	/* Enable bundle and misc interrupts */
	ADF_CSR_WR(pmisc_bar_addr, ADF_DH895XCC_SMIAPF0_MASK_OFFSET,
		   ADF_DH895XCC_SMIA0_MASK);
	ADF_CSR_WR(pmisc_bar_addr, ADF_DH895XCC_SMIAPF1_MASK_OFFSET,
		   ADF_DH895XCC_SMIA1_MASK);

	ret = qat_dev_start(accel_dev);
	if (ret) {
		adf_dev_stop(accel_dev);
		goto out_err;
	}

	return 0;
out_err:
	adf_cleanup_accel(accel_dev);
	return ret;
}

static void __exit adf_remove(struct pci_dev *pdev)
{
	struct adf_accel_dev *accel_dev = adf_devmgr_pci_to_accel_dev(pdev);

	if (!accel_dev) {
		pr_err("QAT: Driver removal failed\n");
		return;
	}
	if (adf_dev_stop(accel_dev))
		dev_err(&GET_DEV(accel_dev), "Failed to stop QAT accel dev\n");
	adf_disable_aer(accel_dev);
	adf_cleanup_accel(accel_dev);
}

static int __init adfdrv_init(void)
{
	request_module("intel_qat");
	if (qat_admin_register())
		return -EFAULT;

	if (pci_register_driver(&adf_driver)) {
		pr_err("QAT: Driver initialization failed\n");
		return -EFAULT;
	}
	return 0;
}

static void __exit adfdrv_release(void)
{
	pci_unregister_driver(&adf_driver);
	qat_admin_unregister();
}

module_init(adfdrv_init);
module_exit(adfdrv_release);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_FIRMWARE("qat_895xcc.bin");
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
