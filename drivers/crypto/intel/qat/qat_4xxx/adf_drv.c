// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2020 Intel Corporation */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <adf_accel_devices.h>
#include <adf_cfg.h>
#include <adf_common_drv.h>
#include <adf_dbgfs.h>

#include "adf_4xxx_hw_data.h"
#include "qat_compression.h"
#include "qat_crypto.h"
#include "adf_transport_access_macros.h"

static const struct pci_device_id adf_pci_tbl[] = {
	{ PCI_VDEVICE(INTEL, ADF_4XXX_PCI_DEVICE_ID), },
	{ PCI_VDEVICE(INTEL, ADF_401XX_PCI_DEVICE_ID), },
	{ PCI_VDEVICE(INTEL, ADF_402XX_PCI_DEVICE_ID), },
	{ }
};
MODULE_DEVICE_TABLE(pci, adf_pci_tbl);

enum configs {
	DEV_CFG_CY = 0,
	DEV_CFG_DC,
	DEV_CFG_SYM,
	DEV_CFG_ASYM,
	DEV_CFG_ASYM_SYM,
	DEV_CFG_ASYM_DC,
	DEV_CFG_DC_ASYM,
	DEV_CFG_SYM_DC,
	DEV_CFG_DC_SYM,
};

static const char * const services_operations[] = {
	ADF_CFG_CY,
	ADF_CFG_DC,
	ADF_CFG_SYM,
	ADF_CFG_ASYM,
	ADF_CFG_ASYM_SYM,
	ADF_CFG_ASYM_DC,
	ADF_CFG_DC_ASYM,
	ADF_CFG_SYM_DC,
	ADF_CFG_DC_SYM,
};

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

static int adf_cfg_dev_init(struct adf_accel_dev *accel_dev)
{
	const char *config;
	int ret;

	config = accel_dev->accel_id % 2 ? ADF_CFG_DC : ADF_CFG_CY;

	ret = adf_cfg_section_add(accel_dev, ADF_GENERAL_SEC);
	if (ret)
		return ret;

	/* Default configuration is crypto only for even devices
	 * and compression for odd devices
	 */
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_GENERAL_SEC,
					  ADF_SERVICES_ENABLED, config,
					  ADF_STR);
	if (ret)
		return ret;

	return 0;
}

static int adf_crypto_dev_config(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	int banks = GET_MAX_BANKS(accel_dev);
	int cpus = num_online_cpus();
	unsigned long bank, val;
	int instances;
	int ret;
	int i;

	if (adf_hw_dev_has_crypto(accel_dev))
		instances = min(cpus, banks / 2);
	else
		instances = 0;

	for (i = 0; i < instances; i++) {
		val = i;
		bank = i * 2;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_BANK_NUM, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &bank, ADF_DEC);
		if (ret)
			goto err;

		bank += 1;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_BANK_NUM, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &bank, ADF_DEC);
		if (ret)
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_ETRMGR_CORE_AFFINITY,
			 i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_SIZE, i);
		val = 128;
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 512;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_SIZE, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 0;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_TX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 0;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_TX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 1;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_RX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 1;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_RX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = ADF_COALESCING_DEF_TIME;
		snprintf(key, sizeof(key), ADF_ETRMGR_COALESCE_TIMER_FORMAT, i);
		ret = adf_cfg_add_key_value_param(accel_dev, "Accelerator0",
						  key, &val, ADF_DEC);
		if (ret)
			goto err;
	}

	val = i;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_CY,
					  &val, ADF_DEC);
	if (ret)
		goto err;

	val = 0;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_DC,
					  &val, ADF_DEC);
	if (ret)
		goto err;

	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to add configuration for crypto\n");
	return ret;
}

static int adf_comp_dev_config(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	int banks = GET_MAX_BANKS(accel_dev);
	int cpus = num_online_cpus();
	unsigned long val;
	int instances;
	int ret;
	int i;

	if (adf_hw_dev_has_compression(accel_dev))
		instances = min(cpus, banks);
	else
		instances = 0;

	for (i = 0; i < instances; i++) {
		val = i;
		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_BANK_NUM, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 512;
		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_SIZE, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 0;
		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_TX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 1;
		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_RX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = ADF_COALESCING_DEF_TIME;
		snprintf(key, sizeof(key), ADF_ETRMGR_COALESCE_TIMER_FORMAT, i);
		ret = adf_cfg_add_key_value_param(accel_dev, "Accelerator0",
						  key, &val, ADF_DEC);
		if (ret)
			goto err;
	}

	val = i;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_DC,
					  &val, ADF_DEC);
	if (ret)
		goto err;

	val = 0;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_CY,
					  &val, ADF_DEC);
	if (ret)
		goto err;

	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to add configuration for compression\n");
	return ret;
}

static int adf_no_dev_config(struct adf_accel_dev *accel_dev)
{
	unsigned long val;
	int ret;

	val = 0;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_DC,
					  &val, ADF_DEC);
	if (ret)
		return ret;

	return adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_CY,
					  &val, ADF_DEC);
}

int adf_gen4_dev_config(struct adf_accel_dev *accel_dev)
{
	char services[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};
	int ret;

	ret = adf_cfg_section_add(accel_dev, ADF_KERNEL_SEC);
	if (ret)
		goto err;

	ret = adf_cfg_section_add(accel_dev, "Accelerator0");
	if (ret)
		goto err;

	ret = adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC,
				      ADF_SERVICES_ENABLED, services);
	if (ret)
		goto err;

	ret = sysfs_match_string(services_operations, services);
	if (ret < 0)
		goto err;

	switch (ret) {
	case DEV_CFG_CY:
	case DEV_CFG_ASYM_SYM:
		ret = adf_crypto_dev_config(accel_dev);
		break;
	case DEV_CFG_DC:
		ret = adf_comp_dev_config(accel_dev);
		break;
	default:
		ret = adf_no_dev_config(accel_dev);
		break;
	}

	if (ret)
		goto err;

	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);

	return ret;

err:
	dev_err(&GET_DEV(accel_dev), "Failed to configure QAT driver\n");
	return ret;
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
	pci_read_config_dword(pdev, ADF_4XXX_FUSECTL4_OFFSET, &hw_data->fuses);

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

	ret = adf_cfg_dev_init(accel_dev);
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
	bar_mask = pci_select_bars(pdev, IORESOURCE_MEM) & ADF_4XXX_BAR_MASK;

	ret = pcim_iomap_regions_request_all(pdev, bar_mask, pci_name(pdev));
	if (ret) {
		dev_err(&pdev->dev, "Failed to map pci regions.\n");
		goto out_err;
	}

	i = 0;
	for_each_set_bit(bar_nr, &bar_mask, PCI_STD_NUM_BARS) {
		bar = &accel_pci_dev->pci_bars[i++];
		bar->virt_addr = pcim_iomap_table(pdev)[bar_nr];
	}

	pci_set_master(pdev);

	if (pci_save_state(pdev)) {
		dev_err(&pdev->dev, "Failed to save pci state.\n");
		ret = -ENOMEM;
		goto out_err;
	}

	adf_dbgfs_init(accel_dev);

	ret = adf_dev_up(accel_dev, true);
	if (ret)
		goto out_err_dev_stop;

	ret = adf_sysfs_init(accel_dev);
	if (ret)
		goto out_err_dev_stop;

	return ret;

out_err_dev_stop:
	adf_dev_down(accel_dev, false);
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
	adf_dev_down(accel_dev, false);
	adf_cleanup_accel(accel_dev);
}

static struct pci_driver adf_driver = {
	.id_table = adf_pci_tbl,
	.name = ADF_4XXX_DEVICE_NAME,
	.probe = adf_probe,
	.remove = adf_remove,
	.sriov_configure = adf_sriov_configure,
	.err_handler = &adf_err_handler,
};

module_pci_driver(adf_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Intel");
MODULE_FIRMWARE(ADF_4XXX_FW);
MODULE_FIRMWARE(ADF_4XXX_MMP);
MODULE_DESCRIPTION("Intel(R) QuickAssist Technology");
MODULE_VERSION(ADF_DRV_VERSION);
MODULE_SOFTDEP("pre: crypto-intel_qat");
