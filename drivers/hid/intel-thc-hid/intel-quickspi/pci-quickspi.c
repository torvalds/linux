/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/pci.h>

#include "intel-thc-dev.h"
#include "intel-thc-hw.h"

#include "quickspi-dev.h"

struct quickspi_driver_data mtl = {
	.max_packet_size_value = MAX_PACKET_SIZE_VALUE_MTL,
};

struct quickspi_driver_data lnl = {
	.max_packet_size_value = MAX_PACKET_SIZE_VALUE_LNL,
};

struct quickspi_driver_data ptl = {
	.max_packet_size_value = MAX_PACKET_SIZE_VALUE_LNL,
};

/* THC QuickSPI ACPI method to get device properties */
/* HIDSPI Method: {6e2ac436-0fcf-41af-a265-b32a220dcfab} */
static guid_t hidspi_guid =
	GUID_INIT(0x6e2ac436, 0x0fcf, 0x41af, 0xa2, 0x65, 0xb3, 0x2a,
		  0x22, 0x0d, 0xcf, 0xab);

/* QuickSpi Method: {300D35b7-ac20-413e-8e9c-92e4dafd0afe} */
static guid_t thc_quickspi_guid =
	GUID_INIT(0x300d35b7, 0xac20, 0x413e, 0x8e, 0x9c, 0x92, 0xe4,
		  0xda, 0xfd, 0x0a, 0xfe);

/* Platform Method: {84005682-5b71-41a4-0x8d668130f787a138} */
static guid_t thc_platform_guid =
	GUID_INIT(0x84005682, 0x5b71, 0x41a4, 0x8d, 0x66, 0x81, 0x30,
		  0xf7, 0x87, 0xa1, 0x38);

/**
 * thc_acpi_get_property - Query device ACPI parameter
 *
 * @adev: point to ACPI device
 * @guid: ACPI method's guid
 * @rev: ACPI method's revision
 * @func: ACPI method's function number
 * @type: ACPI parameter's data type
 * @prop_buf: point to return buffer
 *
 * This is a helper function for device to query its ACPI parameters.
 *
 * Return: 0 if successful or ENODEV on failed.
 */
static int thc_acpi_get_property(struct acpi_device *adev, const guid_t *guid,
				 u64 rev, u64 func, acpi_object_type type, void *prop_buf)
{
	acpi_handle handle = acpi_device_handle(adev);
	union acpi_object *obj;

	obj = acpi_evaluate_dsm_typed(handle, guid, rev, func, NULL, type);
	if (!obj) {
		acpi_handle_err(handle,
				"Error _DSM call failed, rev: %llu, func: %llu, type: %u\n",
				rev, func, type);
		return -ENODEV;
	}

	if (type == ACPI_TYPE_INTEGER)
		*(u32 *)prop_buf = (u32)obj->integer.value;
	else if (type == ACPI_TYPE_BUFFER)
		memcpy(prop_buf, obj->buffer.pointer, obj->buffer.length);

	ACPI_FREE(obj);

	return 0;
}

/**
 * quickspi_get_acpi_resources - Query all quickspi devices' ACPI parameters
 *
 * @qsdev: point to quickspi device
 *
 * This function gets all quickspi devices' ACPI resource.
 *
 * Return: 0 if successful or error code on failed.
 */
static int quickspi_get_acpi_resources(struct quickspi_device *qsdev)
{
	struct acpi_device *adev = ACPI_COMPANION(qsdev->dev);
	int ret = -EINVAL;

	if (!adev) {
		dev_err(qsdev->dev, "no valid ACPI companion\n");
		return ret;
	}

	qsdev->acpi_dev = adev;

	ret = thc_acpi_get_property(adev, &hidspi_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_INPUT_REP_HDR_ADDR,
				    ACPI_TYPE_INTEGER,
				    &qsdev->input_report_hdr_addr);
	if (ret)
		return ret;

	ret = thc_acpi_get_property(adev, &hidspi_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_INPUT_REP_BDY_ADDR,
				    ACPI_TYPE_INTEGER,
				    &qsdev->input_report_bdy_addr);
	if (ret)
		return ret;

	ret = thc_acpi_get_property(adev, &hidspi_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_OUTPUT_REP_ADDR,
				    ACPI_TYPE_INTEGER,
				    &qsdev->output_report_addr);
	if (ret)
		return ret;

	ret = thc_acpi_get_property(adev, &hidspi_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_READ_OPCODE,
				    ACPI_TYPE_BUFFER,
				    &qsdev->spi_read_opcode);
	if (ret)
		return ret;

	ret = thc_acpi_get_property(adev, &hidspi_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_WRITE_OPCODE,
				    ACPI_TYPE_BUFFER,
				    &qsdev->spi_write_opcode);
	if (ret)
		return ret;

	ret = thc_acpi_get_property(adev, &hidspi_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_IO_MODE,
				    ACPI_TYPE_INTEGER,
				    &qsdev->spi_read_io_mode);
	if (ret)
		return ret;

	if (qsdev->spi_read_io_mode & SPI_WRITE_IO_MODE)
		qsdev->spi_write_io_mode = FIELD_GET(SPI_IO_MODE_OPCODE, qsdev->spi_read_io_mode);
	else
		qsdev->spi_write_io_mode = THC_SINGLE_IO;

	qsdev->spi_read_io_mode = FIELD_GET(SPI_IO_MODE_OPCODE, qsdev->spi_read_io_mode);

	ret = thc_acpi_get_property(adev, &thc_quickspi_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_CONNECTION_SPEED,
				    ACPI_TYPE_INTEGER,
				    &qsdev->spi_freq_val);
	if (ret)
		return ret;

	ret = thc_acpi_get_property(adev, &thc_quickspi_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_LIMIT_PACKET_SIZE,
				    ACPI_TYPE_INTEGER,
				    &qsdev->limit_packet_size);
	if (ret)
		return ret;

	if (qsdev->limit_packet_size || !qsdev->driver_data)
		qsdev->spi_packet_size = DEFAULT_MIN_PACKET_SIZE_VALUE;
	else
		qsdev->spi_packet_size = qsdev->driver_data->max_packet_size_value;

	ret = thc_acpi_get_property(adev, &thc_quickspi_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_PERFORMANCE_LIMIT,
				    ACPI_TYPE_INTEGER,
				    &qsdev->performance_limit);
	if (ret)
		return ret;

	qsdev->performance_limit = FIELD_GET(PERFORMANCE_LIMITATION, qsdev->performance_limit);

	ret = thc_acpi_get_property(adev, &thc_platform_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_ACTIVE_LTR,
				    ACPI_TYPE_INTEGER,
				    &qsdev->active_ltr_val);
	if (ret)
		return ret;

	ret = thc_acpi_get_property(adev, &thc_platform_guid,
				    ACPI_QUICKSPI_REVISION_NUM,
				    ACPI_QUICKSPI_FUNC_NUM_LP_LTR,
				    ACPI_TYPE_INTEGER,
				    &qsdev->low_power_ltr_val);
	if (ret)
		return ret;

	return 0;
}

/**
 * quickspi_irq_quick_handler - The ISR of the quickspi driver
 *
 * @irq: The irq number
 * @dev_id: pointer to the device structure
 *
 * Return: IRQ_WAKE_THREAD if further process needed.
 */
static irqreturn_t quickspi_irq_quick_handler(int irq, void *dev_id)
{
	struct quickspi_device *qsdev = dev_id;

	if (qsdev->state == QUICKSPI_DISABLED)
		return IRQ_HANDLED;

	/* Disable THC interrupt before current interrupt be handled */
	thc_interrupt_enable(qsdev->thc_hw, false);

	return IRQ_WAKE_THREAD;
}

/**
 * quickspi_irq_thread_handler - IRQ thread handler of quickspi driver
 *
 * @irq: The IRQ number
 * @dev_id: pointer to the quickspi device structure
 *
 * Return: IRQ_HANDLED to finish this handler.
 */
static irqreturn_t quickspi_irq_thread_handler(int irq, void *dev_id)
{
	struct quickspi_device *qsdev = dev_id;
	int int_mask;

	if (qsdev->state == QUICKSPI_DISABLED)
		return IRQ_HANDLED;

	int_mask = thc_interrupt_handler(qsdev->thc_hw);

	thc_interrupt_enable(qsdev->thc_hw, true);

	return IRQ_HANDLED;
}

/**
 * quickspi_dev_init - Initialize quickspi device
 *
 * @pdev: pointer to the thc pci device
 * @mem_addr: The pointer of MMIO memory address
 * @id: point to pci_device_id structure
 *
 * Alloc quickspi device structure and initialized THC device,
 * then configure THC to HIDSPI mode.
 *
 * If success, enable THC hardware interrupt.
 *
 * Return: pointer to the quickspi device structure if success
 * or NULL on failed.
 */
static struct quickspi_device *quickspi_dev_init(struct pci_dev *pdev, void __iomem *mem_addr,
						 const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct quickspi_device *qsdev;
	int ret;

	qsdev = devm_kzalloc(dev, sizeof(struct quickspi_device), GFP_KERNEL);
	if (!qsdev)
		return ERR_PTR(-ENOMEM);

	qsdev->pdev = pdev;
	qsdev->dev = dev;
	qsdev->mem_addr = mem_addr;
	qsdev->driver_data = (struct quickspi_driver_data *)id->driver_data;

	/* thc hw init */
	qsdev->thc_hw = thc_dev_init(qsdev->dev, qsdev->mem_addr);
	if (IS_ERR(qsdev->thc_hw)) {
		ret = PTR_ERR(qsdev->thc_hw);
		dev_err(dev, "Failed to initialize THC device context, ret = %d.\n", ret);
		return ERR_PTR(ret);
	}

	ret = thc_port_select(qsdev->thc_hw, THC_PORT_TYPE_SPI);
	if (ret) {
		dev_err(dev, "Failed to select THC port, ret = %d.\n", ret);
		return ERR_PTR(ret);
	}

	ret = quickspi_get_acpi_resources(qsdev);
	if (ret) {
		dev_err(dev, "Get ACPI resources failed, ret = %d\n", ret);
		return ERR_PTR(ret);
	}

	thc_interrupt_config(qsdev->thc_hw);

	thc_interrupt_enable(qsdev->thc_hw, true);

	return qsdev;
}

/**
 * quickspi_dev_deinit - De-initialize quickspi device
 *
 * @qsdev: pointer to the quickspi device structure
 *
 * Disable THC interrupt and deinitilize THC.
 */
static void quickspi_dev_deinit(struct quickspi_device *qsdev)
{
	thc_interrupt_enable(qsdev->thc_hw, false);
}

/*
 * quickspi_probe: Quickspi driver probe function
 *
 * @pdev: point to pci device
 * @id: point to pci_device_id structure
 *
 * Return 0 if success or error code on failure.
 */
static int quickspi_probe(struct pci_dev *pdev,
			  const struct pci_device_id *id)
{
	struct quickspi_device *qsdev;
	void __iomem *mem_addr;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PCI device, ret = %d.\n", ret);
		return ret;
	}

	pci_set_master(pdev);

	ret = pcim_iomap_regions(pdev, BIT(0), KBUILD_MODNAME);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get PCI regions, ret = %d.\n", ret);
		goto disable_pci_device;
	}

	mem_addr = pcim_iomap_table(pdev)[0];

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
	if (ret) {
		ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
		if (ret) {
			dev_err(&pdev->dev, "No usable DMA configuration %d\n", ret);
			goto unmap_io_region;
		}
	}

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Failed to allocate IRQ vectors. ret = %d\n", ret);
		goto unmap_io_region;
	}

	pdev->irq = pci_irq_vector(pdev, 0);

	qsdev = quickspi_dev_init(pdev, mem_addr, id);
	if (IS_ERR(qsdev)) {
		dev_err(&pdev->dev, "QuickSPI device init failed\n");
		ret = PTR_ERR(qsdev);
		goto unmap_io_region;
	}

	pci_set_drvdata(pdev, qsdev);

	ret = devm_request_threaded_irq(&pdev->dev, pdev->irq,
					quickspi_irq_quick_handler,
					quickspi_irq_thread_handler,
					IRQF_ONESHOT, KBUILD_MODNAME,
					qsdev);
	if (ret) {
		dev_err(&pdev->dev,
			"Failed to request threaded IRQ, irq = %d.\n", pdev->irq);
		goto dev_deinit;
	}

	return 0;

dev_deinit:
	quickspi_dev_deinit(qsdev);
unmap_io_region:
	pcim_iounmap_regions(pdev, BIT(0));
disable_pci_device:
	pci_clear_master(pdev);

	return ret;
}

/**
 * quickspi_remove - Device Removal Routine
 *
 * @pdev: PCI device structure
 *
 * This is called by the PCI subsystem to alert the driver
 * that it should release a PCI device.
 */
static void quickspi_remove(struct pci_dev *pdev)
{
	struct quickspi_device *qsdev;

	qsdev = pci_get_drvdata(pdev);
	if (!qsdev)
		return;

	quickspi_dev_deinit(qsdev);

	pcim_iounmap_regions(pdev, BIT(0));
	pci_clear_master(pdev);
}

/**
 * quickspi_shutdown - Device Shutdown Routine
 *
 * @pdev: PCI device structure
 *
 * This is called from the reboot notifier
 * it's a simplified version of remove so we go down
 * faster.
 */
static void quickspi_shutdown(struct pci_dev *pdev)
{
	struct quickspi_device *qsdev;

	qsdev = pci_get_drvdata(pdev);
	if (!qsdev)
		return;

	quickspi_dev_deinit(qsdev);
}

static const struct pci_device_id quickspi_pci_tbl[] = {
	{PCI_DEVICE_DATA(INTEL, THC_MTL_DEVICE_ID_SPI_PORT1, &mtl), },
	{PCI_DEVICE_DATA(INTEL, THC_MTL_DEVICE_ID_SPI_PORT2, &mtl), },
	{PCI_DEVICE_DATA(INTEL, THC_LNL_DEVICE_ID_SPI_PORT1, &lnl), },
	{PCI_DEVICE_DATA(INTEL, THC_LNL_DEVICE_ID_SPI_PORT2, &lnl), },
	{PCI_DEVICE_DATA(INTEL, THC_PTL_H_DEVICE_ID_SPI_PORT1, &ptl), },
	{PCI_DEVICE_DATA(INTEL, THC_PTL_H_DEVICE_ID_SPI_PORT2, &ptl), },
	{PCI_DEVICE_DATA(INTEL, THC_PTL_U_DEVICE_ID_SPI_PORT1, &ptl), },
	{PCI_DEVICE_DATA(INTEL, THC_PTL_U_DEVICE_ID_SPI_PORT2, &ptl), },
	{}
};
MODULE_DEVICE_TABLE(pci, quickspi_pci_tbl);

static struct pci_driver quickspi_driver = {
	.name = KBUILD_MODNAME,
	.id_table = quickspi_pci_tbl,
	.probe = quickspi_probe,
	.remove = quickspi_remove,
	.shutdown = quickspi_shutdown,
	.driver.probe_type = PROBE_PREFER_ASYNCHRONOUS,
};

module_pci_driver(quickspi_driver);

MODULE_AUTHOR("Xinpeng Sun <xinpeng.sun@intel.com>");
MODULE_AUTHOR("Even Xu <even.xu@intel.com>");

MODULE_DESCRIPTION("Intel(R) QuickSPI Driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("INTEL_THC");
