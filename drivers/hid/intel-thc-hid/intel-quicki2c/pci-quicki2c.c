/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/pci.h>

#include "intel-thc-dev.h"

#include "quicki2c-dev.h"

/**
 * quicki2c_irq_quick_handler - The ISR of the quicki2c driver
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * Return: IRQ_WAKE_THREAD if further process needed.
 */
static irqreturn_t quicki2c_irq_quick_handler(int irq, void *dev_id)
{
	struct quicki2c_device *qcdev = dev_id;

	if (qcdev->state == QUICKI2C_DISABLED)
		return IRQ_HANDLED;

	/* Disable THC interrupt before current interrupt be handled */
	thc_interrupt_enable(qcdev->thc_hw, false);

	return IRQ_WAKE_THREAD;
}

/**
 * quicki2c_irq_thread_handler - IRQ thread handler of quicki2c driver
 *
 * @irq: The IRQ number
 * @dev_id: pointer to the quicki2c device structure
 *
 * Return: IRQ_HANDLED to finish this handler.
 */
static irqreturn_t quicki2c_irq_thread_handler(int irq, void *dev_id)
{
	struct quicki2c_device *qcdev = dev_id;
	int int_mask;

	if (qcdev->state == QUICKI2C_DISABLED)
		return IRQ_HANDLED;

	int_mask = thc_interrupt_handler(qcdev->thc_hw);

	thc_interrupt_enable(qcdev->thc_hw, true);

	return IRQ_HANDLED;
}

/**
 * quicki2c_dev_init - Initialize quicki2c device
 *
 * @pdev: pointer to the thc pci device
 * @mem_addr: The pointer of MMIO memory address
 *
 * Alloc quicki2c device structure and initialized THC device,
 * then configure THC to HIDI2C mode.
 *
 * If success, enable THC hardware interrupt.
 *
 * Return: pointer to the quicki2c device structure if success
 * or NULL on failed.
 */
static struct quicki2c_device *quicki2c_dev_init(struct pci_dev *pdev, void __iomem *mem_addr)
{
	struct device *dev = &pdev->dev;
	struct quicki2c_device *qcdev;
	int ret;

	qcdev = devm_kzalloc(dev, sizeof(struct quicki2c_device), GFP_KERNEL);
	if (!qcdev)
		return ERR_PTR(-ENOMEM);

	qcdev->pdev = pdev;
	qcdev->dev = dev;
	qcdev->mem_addr = mem_addr;

	/* thc hw init */
	qcdev->thc_hw = thc_dev_init(qcdev->dev, qcdev->mem_addr);
	if (IS_ERR(qcdev->thc_hw)) {
		ret = PTR_ERR(qcdev->thc_hw);
		dev_err_once(dev, "Failed to initialize THC device context, ret = %d.\n", ret);
		return ERR_PTR(ret);
	}

	ret = thc_port_select(qcdev->thc_hw, THC_PORT_TYPE_I2C);
	if (ret) {
		dev_err_once(dev, "Failed to select THC port, ret = %d.\n", ret);
		return ERR_PTR(ret);
	}

	thc_interrupt_config(qcdev->thc_hw);

	thc_interrupt_enable(qcdev->thc_hw, true);

	return qcdev;
}

/**
 * quicki2c_dev_deinit - De-initialize quicki2c device
 *
 * @qcdev: pointer to the quicki2c device structure
 *
 * Disable THC interrupt and deinitilize THC.
 */
static void quicki2c_dev_deinit(struct quicki2c_device *qcdev)
{
	thc_interrupt_enable(qcdev->thc_hw, false);
}

/*
 * quicki2c_probe: Quicki2c driver probe function
 *
 * @pdev: point to pci device
 * @id: point to pci_device_id structure
 *
 * Return 0 if success or error code on failed.
 */
static int quicki2c_probe(struct pci_dev *pdev,
			  const struct pci_device_id *id)
{
	struct quicki2c_device *qcdev;
	void __iomem *mem_addr;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err_once(&pdev->dev, "Failed to enable PCI device, ret = %d.\n", ret);
		return ret;
	}

	pci_set_master(pdev);

	ret = pcim_iomap_regions(pdev, BIT(0), KBUILD_MODNAME);
	if (ret) {
		dev_err_once(&pdev->dev, "Failed to get PCI regions, ret = %d.\n", ret);
		goto disable_pci_device;
	}

	mem_addr = pcim_iomap_table(pdev)[0];

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err_once(&pdev->dev, "No usable DMA configuration %d\n", ret);
			goto unmap_io_region;
		}
	}

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0) {
		dev_err_once(&pdev->dev,
			     "Failed to allocate IRQ vectors. ret = %d\n", ret);
		goto unmap_io_region;
	}

	pdev->irq = pci_irq_vector(pdev, 0);

	qcdev = quicki2c_dev_init(pdev, mem_addr);
	if (IS_ERR(qcdev)) {
		dev_err_once(&pdev->dev, "QuickI2C device init failed\n");
		ret = PTR_ERR(qcdev);
		goto unmap_io_region;
	}

	pci_set_drvdata(pdev, qcdev);

	ret = devm_request_threaded_irq(&pdev->dev, pdev->irq,
					quicki2c_irq_quick_handler,
					quicki2c_irq_thread_handler,
					IRQF_ONESHOT, KBUILD_MODNAME,
					qcdev);
	if (ret) {
		dev_err_once(&pdev->dev,
			     "Failed to request threaded IRQ, irq = %d.\n", pdev->irq);
		goto dev_deinit;
	}

	return 0;

dev_deinit:
	quicki2c_dev_deinit(qcdev);
unmap_io_region:
	pcim_iounmap_regions(pdev, BIT(0));
disable_pci_device:
	pci_clear_master(pdev);

	return ret;
}

/**
 * quicki2c_remove - Device Removal Routine
 *
 * @pdev: PCI device structure
 *
 * This is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.
 */
static void quicki2c_remove(struct pci_dev *pdev)
{
	struct quicki2c_device *qcdev;

	qcdev = pci_get_drvdata(pdev);
	if (!qcdev)
		return;

	quicki2c_dev_deinit(qcdev);

	pcim_iounmap_regions(pdev, BIT(0));
	pci_clear_master(pdev);
}

/**
 * quicki2c_shutdown - Device Shutdown Routine
 *
 * @pdev: PCI device structure
 *
 * This is called from the reboot notifier
 * it's a simplified version of remove so we go down
 * faster.
 */
static void quicki2c_shutdown(struct pci_dev *pdev)
{
	struct quicki2c_device *qcdev;

	qcdev = pci_get_drvdata(pdev);
	if (!qcdev)
		return;

	quicki2c_dev_deinit(qcdev);
}

static const struct pci_device_id quicki2c_pci_tbl[] = {
	{PCI_VDEVICE(INTEL, THC_LNL_DEVICE_ID_I2C_PORT1), },
	{PCI_VDEVICE(INTEL, THC_LNL_DEVICE_ID_I2C_PORT2), },
	{PCI_VDEVICE(INTEL, THC_PTL_H_DEVICE_ID_I2C_PORT1), },
	{PCI_VDEVICE(INTEL, THC_PTL_H_DEVICE_ID_I2C_PORT2), },
	{PCI_VDEVICE(INTEL, THC_PTL_U_DEVICE_ID_I2C_PORT1), },
	{PCI_VDEVICE(INTEL, THC_PTL_U_DEVICE_ID_I2C_PORT2), },
	{}
};
MODULE_DEVICE_TABLE(pci, quicki2c_pci_tbl);

static struct pci_driver quicki2c_driver = {
	.name = KBUILD_MODNAME,
	.id_table = quicki2c_pci_tbl,
	.probe = quicki2c_probe,
	.remove = quicki2c_remove,
	.shutdown = quicki2c_shutdown,
	.driver.probe_type = PROBE_PREFER_ASYNCHRONOUS,
};

module_pci_driver(quicki2c_driver);

MODULE_AUTHOR("Xinpeng Sun <xinpeng.sun@intel.com>");
MODULE_AUTHOR("Even Xu <even.xu@intel.com>");

MODULE_DESCRIPTION("Intel(R) QuickI2C Driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("INTEL_THC");
