/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2024 Intel Corporation */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/irqreturn.h>
#include <linux/pci.h>
#include <linux/sizes.h>
#include <linux/pm_runtime.h>

#include "intel-thc-dev.h"
#include "intel-thc-hw.h"

#include "quicki2c-dev.h"
#include "quicki2c-hid.h"
#include "quicki2c-protocol.h"

/* THC QuickI2C ACPI method to get device properties */
/* HIDI2C device method */
static guid_t i2c_hid_guid =
	GUID_INIT(0x3cdff6f7, 0x4267, 0x4555, 0xad, 0x05, 0xb3, 0x0a, 0x3d, 0x89, 0x38, 0xde);

/* platform method */
static guid_t thc_platform_guid =
	GUID_INIT(0x84005682, 0x5b71, 0x41a4, 0x8d, 0x66, 0x81, 0x30, 0xf7, 0x87, 0xa1, 0x38);

/**
 * quicki2c_acpi_get_dsm_property - Query device ACPI DSM parameter
 *
 * @adev: point to ACPI device
 * @guid: ACPI method's guid
 * @rev: ACPI method's revision
 * @func: ACPI method's function number
 * @type: ACPI parameter's data type
 * @prop_buf: point to return buffer
 *
 * This is a helper function for device to query its ACPI DSM parameters.
 *
 * Return: 0 if success or ENODEV on failed.
 */
static int quicki2c_acpi_get_dsm_property(struct acpi_device *adev, const guid_t *guid,
					  u64 rev, u64 func, acpi_object_type type, void *prop_buf)
{
	acpi_handle handle = acpi_device_handle(adev);
	union acpi_object *obj;

	obj = acpi_evaluate_dsm_typed(handle, guid, rev, func, NULL, type);
	if (!obj) {
		acpi_handle_err(handle,
				"Error _DSM call failed, rev: %d, func: %d, type: %d\n",
				(int)rev, (int)func, (int)type);
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
 * quicki2c_acpi_get_dsd_property - Query device ACPI DSD parameter
 *
 * @adev: point to ACPI device
 * @dsd_method_name: ACPI method's property name
 * @type: ACPI parameter's data type
 * @prop_buf: point to return buffer
 *
 * This is a helper function for device to query its ACPI DSD parameters.
 *
 * Return: 0 if success or ENODEV on failed.
 */
static int quicki2c_acpi_get_dsd_property(struct acpi_device *adev, acpi_string dsd_method_name,
					  acpi_object_type type, void *prop_buf)
{
	acpi_handle handle = acpi_device_handle(adev);
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object obj = { .type = type };
	struct acpi_object_list arg_list = {
		.count = 1,
		.pointer = &obj,
	};
	union acpi_object *ret_obj;
	acpi_status status;

	status = acpi_evaluate_object(handle, dsd_method_name, &arg_list, &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_handle_err(handle,
				"Can't evaluate %s method: %d\n", dsd_method_name, status);
		return -ENODEV;
	}

	ret_obj = buffer.pointer;

	memcpy(prop_buf, ret_obj->buffer.pointer, ret_obj->buffer.length);

	return 0;
}

/**
 * quicki2c_get_acpi_resources - Query all quicki2c devices' ACPI parameters
 *
 * @qcdev: point to quicki2c device
 *
 * This function gets all quicki2c devices' ACPI resource.
 *
 * Return: 0 if success or error code on failed.
 */
static int quicki2c_get_acpi_resources(struct quicki2c_device *qcdev)
{
	struct acpi_device *adev = ACPI_COMPANION(qcdev->dev);
	struct quicki2c_subip_acpi_parameter i2c_param;
	struct quicki2c_subip_acpi_config i2c_config;
	u32 hid_desc_addr;
	int ret = -EINVAL;

	if (!adev) {
		dev_err(qcdev->dev, "Invalid acpi device pointer\n");
		return ret;
	}

	qcdev->acpi_dev = adev;

	ret = quicki2c_acpi_get_dsm_property(adev, &i2c_hid_guid,
					     QUICKI2C_ACPI_REVISION_NUM,
					     QUICKI2C_ACPI_FUNC_NUM_HID_DESC_ADDR,
					     ACPI_TYPE_INTEGER,
					     &hid_desc_addr);
	if (ret)
		return ret;

	qcdev->hid_desc_addr = (u16)hid_desc_addr;

	ret = quicki2c_acpi_get_dsm_property(adev, &thc_platform_guid,
					     QUICKI2C_ACPI_REVISION_NUM,
					     QUICKI2C_ACPI_FUNC_NUM_ACTIVE_LTR_VAL,
					     ACPI_TYPE_INTEGER,
					     &qcdev->active_ltr_val);
	if (ret)
		return ret;

	ret = quicki2c_acpi_get_dsm_property(adev, &thc_platform_guid,
					     QUICKI2C_ACPI_REVISION_NUM,
					     QUICKI2C_ACPI_FUNC_NUM_LP_LTR_VAL,
					     ACPI_TYPE_INTEGER,
					     &qcdev->low_power_ltr_val);
	if (ret)
		return ret;

	ret = quicki2c_acpi_get_dsd_property(adev, QUICKI2C_ACPI_METHOD_NAME_ICRS,
					     ACPI_TYPE_BUFFER, &i2c_param);
	if (ret)
		return ret;

	if (i2c_param.addressing_mode != HIDI2C_ADDRESSING_MODE_7BIT)
		return -EOPNOTSUPP;

	qcdev->i2c_slave_addr = i2c_param.device_address;

	ret = quicki2c_acpi_get_dsd_property(adev, QUICKI2C_ACPI_METHOD_NAME_ISUB,
					     ACPI_TYPE_BUFFER, &i2c_config);
	if (ret)
		return ret;

	if (i2c_param.connection_speed > 0 &&
	    i2c_param.connection_speed <= QUICKI2C_SUBIP_STANDARD_MODE_MAX_SPEED) {
		qcdev->i2c_speed_mode = THC_I2C_STANDARD;
		qcdev->i2c_clock_hcnt = i2c_config.SMHX;
		qcdev->i2c_clock_lcnt = i2c_config.SMLX;
	} else if (i2c_param.connection_speed > QUICKI2C_SUBIP_STANDARD_MODE_MAX_SPEED &&
		   i2c_param.connection_speed <= QUICKI2C_SUBIP_FAST_MODE_MAX_SPEED) {
		qcdev->i2c_speed_mode = THC_I2C_FAST_AND_PLUS;
		qcdev->i2c_clock_hcnt = i2c_config.FMHX;
		qcdev->i2c_clock_lcnt = i2c_config.FMLX;
	} else if (i2c_param.connection_speed > QUICKI2C_SUBIP_FAST_MODE_MAX_SPEED &&
		   i2c_param.connection_speed <= QUICKI2C_SUBIP_FASTPLUS_MODE_MAX_SPEED) {
		qcdev->i2c_speed_mode = THC_I2C_FAST_AND_PLUS;
		qcdev->i2c_clock_hcnt = i2c_config.FPHX;
		qcdev->i2c_clock_lcnt = i2c_config.FPLX;
	} else if (i2c_param.connection_speed > QUICKI2C_SUBIP_FASTPLUS_MODE_MAX_SPEED &&
		   i2c_param.connection_speed <= QUICKI2C_SUBIP_HIGH_SPEED_MODE_MAX_SPEED) {
		qcdev->i2c_speed_mode = THC_I2C_HIGH_SPEED;
		qcdev->i2c_clock_hcnt = i2c_config.HMHX;
		qcdev->i2c_clock_lcnt = i2c_config.HMLX;
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

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
 * try_recover - Try to recovery THC and Device
 * @qcdev: pointer to quicki2c device
 *
 * This function is a error handler, called when fatal error happens.
 * It try to reset Touch Device and re-configure THC to recovery
 * transferring between Device and THC.
 *
 * Return: 0 if successful or error code on failed
 */
static int try_recover(struct quicki2c_device *qcdev)
{
	int ret;

	thc_dma_unconfigure(qcdev->thc_hw);

	ret = thc_dma_configure(qcdev->thc_hw);
	if (ret) {
		dev_err(qcdev->dev, "Reconfig DMA failed\n");
		return ret;
	}

	return 0;
}

static int handle_input_report(struct quicki2c_device *qcdev)
{
	struct hidi2c_report_packet *pkt = (struct hidi2c_report_packet *)qcdev->input_buf;
	int rx_dma_finished = 0;
	size_t report_len;
	int ret;

	while (!rx_dma_finished) {
		ret = thc_rxdma_read(qcdev->thc_hw, THC_RXDMA2,
				     (u8 *)pkt, &report_len,
				     &rx_dma_finished);
		if (ret)
			return ret;

		if (!pkt->len) {
			if (qcdev->state == QUICKI2C_RESETING) {
				qcdev->reset_ack = true;
				wake_up(&qcdev->reset_ack_wq);

				qcdev->state = QUICKI2C_RESETED;
			} else {
				dev_warn(qcdev->dev, "unexpected DIR happen\n");
			}

			continue;
		}

		/* discard samples before driver probe complete */
		if (qcdev->state != QUICKI2C_ENABLED)
			continue;

		quicki2c_hid_send_report(qcdev, pkt->data,
					 HIDI2C_DATA_LEN(le16_to_cpu(pkt->len)));
	}

	return 0;
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
	int err_recover = 0;
	int int_mask;
	int ret;

	if (qcdev->state == QUICKI2C_DISABLED)
		return IRQ_HANDLED;

	ret = pm_runtime_resume_and_get(qcdev->dev);
	if (ret)
		return IRQ_HANDLED;

	int_mask = thc_interrupt_handler(qcdev->thc_hw);

	if (int_mask & BIT(THC_FATAL_ERR_INT) || int_mask & BIT(THC_TXN_ERR_INT) ||
	    int_mask & BIT(THC_UNKNOWN_INT)) {
		err_recover = 1;
		goto exit;
	}

	if (int_mask & BIT(THC_RXDMA2_INT)) {
		err_recover = handle_input_report(qcdev);
		if (err_recover)
			goto exit;
	}

exit:
	thc_interrupt_enable(qcdev->thc_hw, true);

	if (err_recover)
		if (try_recover(qcdev))
			qcdev->state = QUICKI2C_DISABLED;

	pm_runtime_mark_last_busy(qcdev->dev);
	pm_runtime_put_autosuspend(qcdev->dev);

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
	qcdev->state = QUICKI2C_DISABLED;

	init_waitqueue_head(&qcdev->reset_ack_wq);

	/* thc hw init */
	qcdev->thc_hw = thc_dev_init(qcdev->dev, qcdev->mem_addr);
	if (IS_ERR(qcdev->thc_hw)) {
		ret = PTR_ERR(qcdev->thc_hw);
		dev_err_once(dev, "Failed to initialize THC device context, ret = %d.\n", ret);
		return ERR_PTR(ret);
	}

	ret = quicki2c_get_acpi_resources(qcdev);
	if (ret) {
		dev_err_once(dev, "Get ACPI resources failed, ret = %d\n", ret);
		return ERR_PTR(ret);
	}

	ret = thc_interrupt_quiesce(qcdev->thc_hw, true);
	if (ret)
		return ERR_PTR(ret);

	ret = thc_port_select(qcdev->thc_hw, THC_PORT_TYPE_I2C);
	if (ret) {
		dev_err_once(dev, "Failed to select THC port, ret = %d.\n", ret);
		return ERR_PTR(ret);
	}

	ret = thc_i2c_subip_init(qcdev->thc_hw, qcdev->i2c_slave_addr,
				 qcdev->i2c_speed_mode,
				 qcdev->i2c_clock_hcnt,
				 qcdev->i2c_clock_lcnt);
	if (ret)
		return ERR_PTR(ret);

	thc_int_trigger_type_select(qcdev->thc_hw, false);

	thc_interrupt_config(qcdev->thc_hw);

	thc_interrupt_enable(qcdev->thc_hw, true);

	qcdev->state = QUICKI2C_INITED;

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
	thc_ltr_unconfig(qcdev->thc_hw);

	qcdev->state = QUICKI2C_DISABLED;
}

/**
 * quicki2c_dma_init - Configure THC DMA for quicki2c device
 * @qcdev: pointer to the quicki2c device structure
 *
 * This function uses TIC's parameters(such as max input length, max output
 * length) to allocate THC DMA buffers and configure THC DMA engines.
 *
 * Return: 0 if success or error code on failed.
 */
static int quicki2c_dma_init(struct quicki2c_device *qcdev)
{
	size_t swdma_max_len;
	int ret;

	swdma_max_len = max(le16_to_cpu(qcdev->dev_desc.max_input_len),
			    le16_to_cpu(qcdev->dev_desc.report_desc_len));

	ret = thc_dma_set_max_packet_sizes(qcdev->thc_hw, 0,
					   le16_to_cpu(qcdev->dev_desc.max_input_len),
					   le16_to_cpu(qcdev->dev_desc.max_output_len),
					   swdma_max_len);
	if (ret)
		return ret;

	ret = thc_dma_allocate(qcdev->thc_hw);
	if (ret) {
		dev_err(qcdev->dev, "Allocate THC DMA buffer failed, ret = %d\n", ret);
		return ret;
	}

	/* Enable RxDMA */
	ret = thc_dma_configure(qcdev->thc_hw);
	if (ret) {
		dev_err(qcdev->dev, "Configure THC DMA failed, ret = %d\n", ret);
		thc_dma_unconfigure(qcdev->thc_hw);
		thc_dma_release(qcdev->thc_hw);
		return ret;
	}

	return ret;
}

/**
 * quicki2c_dma_deinit - Release THC DMA for quicki2c device
 * @qcdev: pointer to the quicki2c device structure
 *
 * Stop THC DMA engines and release all DMA buffers.
 *
 */
static void quicki2c_dma_deinit(struct quicki2c_device *qcdev)
{
	thc_dma_unconfigure(qcdev->thc_hw);
	thc_dma_release(qcdev->thc_hw);
}

/**
 * quicki2c_alloc_report_buf - Alloc report buffers
 * @qcdev: pointer to the quicki2c device structure
 *
 * Allocate report descriptor buffer, it will be used for restore TIC HID
 * report descriptor.
 *
 * Allocate input report buffer, it will be used for receive HID input report
 * data from TIC.
 *
 * Allocate output report buffer, it will be used for store HID output report,
 * such as set feature.
 *
 * Return: 0 if success or error code on failed.
 */
static int quicki2c_alloc_report_buf(struct quicki2c_device *qcdev)
{
	size_t max_report_len;

	qcdev->report_descriptor = devm_kzalloc(qcdev->dev,
						le16_to_cpu(qcdev->dev_desc.report_desc_len),
						GFP_KERNEL);
	if (!qcdev->report_descriptor)
		return -ENOMEM;

	/*
	 * Some HIDI2C devices don't declare input/output max length correctly,
	 * give default 4K buffer to avoid DMA buffer overrun.
	 */
	max_report_len = max(le16_to_cpu(qcdev->dev_desc.max_input_len), SZ_4K);

	qcdev->input_buf = devm_kzalloc(qcdev->dev, max_report_len, GFP_KERNEL);
	if (!qcdev->input_buf)
		return -ENOMEM;

	if (!le16_to_cpu(qcdev->dev_desc.max_output_len))
		qcdev->dev_desc.max_output_len = cpu_to_le16(SZ_4K);

	max_report_len = max(le16_to_cpu(qcdev->dev_desc.max_output_len),
			     max_report_len);

	qcdev->report_buf = devm_kzalloc(qcdev->dev, max_report_len, GFP_KERNEL);
	if (!qcdev->report_buf)
		return -ENOMEM;

	qcdev->report_len = max_report_len;

	return 0;
}

/*
 * quicki2c_probe: Quicki2c driver probe function
 *
 * @pdev: point to pci device
 * @id: point to pci_device_id structure
 *
 * This function initializes THC and HIDI2C device, the flow is:
 * - do THC pci device initialization
 * - query HIDI2C ACPI parameters
 * - configure THC to HIDI2C mode
 * - go through HIDI2C enumeration flow
 *   |- read device descriptor
 *   |- reset HIDI2C device
 * - enable THC interrupt and DMA
 * - read report descriptor
 * - register HID device
 * - enable runtime power management
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

	ret = quicki2c_get_device_descriptor(qcdev);
	if (ret) {
		dev_err(&pdev->dev, "Get device descriptor failed, ret = %d\n", ret);
		goto dev_deinit;
	}

	ret = quicki2c_alloc_report_buf(qcdev);
	if (ret) {
		dev_err(&pdev->dev, "Alloc report buffers failed, ret= %d\n", ret);
		goto dev_deinit;
	}

	ret = quicki2c_dma_init(qcdev);
	if (ret) {
		dev_err(&pdev->dev, "Setup THC DMA failed, ret= %d\n", ret);
		goto dev_deinit;
	}

	ret = thc_interrupt_quiesce(qcdev->thc_hw, false);
	if (ret)
		goto dev_deinit;

	ret = quicki2c_set_power(qcdev, HIDI2C_ON);
	if (ret) {
		dev_err(&pdev->dev, "Set Power On command failed, ret= %d\n", ret);
		goto dev_deinit;
	}

	ret = quicki2c_reset(qcdev);
	if (ret) {
		dev_err(&pdev->dev, "Reset HIDI2C device failed, ret= %d\n", ret);
		goto dev_deinit;
	}

	ret = quicki2c_get_report_descriptor(qcdev);
	if (ret) {
		dev_err(&pdev->dev, "Get report descriptor failed, ret = %d\n", ret);
		goto dma_deinit;
	}

	ret = quicki2c_hid_probe(qcdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register HID device, ret = %d\n", ret);
		goto dma_deinit;
	}

	qcdev->state = QUICKI2C_ENABLED;

	/* Enable runtime power management */
	pm_runtime_use_autosuspend(qcdev->dev);
	pm_runtime_set_autosuspend_delay(qcdev->dev, DEFAULT_AUTO_SUSPEND_DELAY_MS);
	pm_runtime_mark_last_busy(qcdev->dev);
	pm_runtime_put_noidle(qcdev->dev);
	pm_runtime_put_autosuspend(qcdev->dev);

	dev_dbg(&pdev->dev, "QuickI2C probe success\n");

	return 0;

dma_deinit:
	quicki2c_dma_deinit(qcdev);
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

	quicki2c_hid_remove(qcdev);
	quicki2c_dma_deinit(qcdev);

	pm_runtime_get_noresume(qcdev->dev);

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

	/* Must stop DMA before reboot to avoid DMA entering into unknown state */
	quicki2c_dma_deinit(qcdev);

	quicki2c_dev_deinit(qcdev);
}

static int quicki2c_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct quicki2c_device *qcdev;
	int ret;

	qcdev = pci_get_drvdata(pdev);
	if (!qcdev)
		return -ENODEV;

	/*
	 * As I2C is THC subsystem, no register auto save/restore support,
	 * need driver to do that explicitly for every D3 case.
	 */
	ret = thc_i2c_subip_regs_save(qcdev->thc_hw);
	if (ret)
		return ret;

	ret = thc_interrupt_quiesce(qcdev->thc_hw, true);
	if (ret)
		return ret;

	thc_interrupt_enable(qcdev->thc_hw, false);

	thc_dma_unconfigure(qcdev->thc_hw);

	return 0;
}

static int quicki2c_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct quicki2c_device *qcdev;
	int ret;

	qcdev = pci_get_drvdata(pdev);
	if (!qcdev)
		return -ENODEV;

	ret = thc_port_select(qcdev->thc_hw, THC_PORT_TYPE_I2C);
	if (ret)
		return ret;

	ret = thc_i2c_subip_regs_restore(qcdev->thc_hw);
	if (ret)
		return ret;

	thc_interrupt_config(qcdev->thc_hw);

	thc_interrupt_enable(qcdev->thc_hw, true);

	ret = thc_dma_configure(qcdev->thc_hw);
	if (ret)
		return ret;

	ret = thc_interrupt_quiesce(qcdev->thc_hw, false);
	if (ret)
		return ret;

	return 0;
}

static int quicki2c_freeze(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct quicki2c_device *qcdev;
	int ret;

	qcdev = pci_get_drvdata(pdev);
	if (!qcdev)
		return -ENODEV;

	ret = thc_interrupt_quiesce(qcdev->thc_hw, true);
	if (ret)
		return ret;

	thc_interrupt_enable(qcdev->thc_hw, false);

	thc_dma_unconfigure(qcdev->thc_hw);

	return 0;
}

static int quicki2c_thaw(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct quicki2c_device *qcdev;
	int ret;

	qcdev = pci_get_drvdata(pdev);
	if (!qcdev)
		return -ENODEV;

	ret = thc_dma_configure(qcdev->thc_hw);
	if (ret)
		return ret;

	thc_interrupt_enable(qcdev->thc_hw, true);

	ret = thc_interrupt_quiesce(qcdev->thc_hw, false);
	if (ret)
		return ret;

	return 0;
}

static int quicki2c_poweroff(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct quicki2c_device *qcdev;
	int ret;

	qcdev = pci_get_drvdata(pdev);
	if (!qcdev)
		return -ENODEV;

	ret = thc_interrupt_quiesce(qcdev->thc_hw, true);
	if (ret)
		return ret;

	thc_interrupt_enable(qcdev->thc_hw, false);

	thc_ltr_unconfig(qcdev->thc_hw);

	quicki2c_dma_deinit(qcdev);

	return 0;
}

static int quicki2c_restore(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct quicki2c_device *qcdev;
	int ret;

	qcdev = pci_get_drvdata(pdev);
	if (!qcdev)
		return -ENODEV;

	/* Reconfig THC HW when back from hibernate */
	ret = thc_port_select(qcdev->thc_hw, THC_PORT_TYPE_I2C);
	if (ret)
		return ret;

	ret = thc_i2c_subip_init(qcdev->thc_hw, qcdev->i2c_slave_addr,
				 qcdev->i2c_speed_mode,
				 qcdev->i2c_clock_hcnt,
				 qcdev->i2c_clock_lcnt);
	if (ret)
		return ret;

	thc_interrupt_config(qcdev->thc_hw);

	thc_interrupt_enable(qcdev->thc_hw, true);

	ret = thc_interrupt_quiesce(qcdev->thc_hw, false);
	if (ret)
		return ret;

	ret = thc_dma_configure(qcdev->thc_hw);
	if (ret)
		return ret;

	thc_ltr_config(qcdev->thc_hw,
		       qcdev->active_ltr_val,
		       qcdev->low_power_ltr_val);

	thc_change_ltr_mode(qcdev->thc_hw, THC_LTR_MODE_ACTIVE);

	return 0;
}

static int quicki2c_runtime_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct quicki2c_device *qcdev;

	qcdev = pci_get_drvdata(pdev);
	if (!qcdev)
		return -ENODEV;

	thc_change_ltr_mode(qcdev->thc_hw, THC_LTR_MODE_LP);

	pci_save_state(pdev);

	return 0;
}

static int quicki2c_runtime_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct quicki2c_device *qcdev;

	qcdev = pci_get_drvdata(pdev);
	if (!qcdev)
		return -ENODEV;

	thc_change_ltr_mode(qcdev->thc_hw, THC_LTR_MODE_ACTIVE);

	return 0;
}

static const struct dev_pm_ops quicki2c_pm_ops = {
	.suspend = quicki2c_suspend,
	.resume = quicki2c_resume,
	.freeze = quicki2c_freeze,
	.thaw = quicki2c_thaw,
	.poweroff = quicki2c_poweroff,
	.restore = quicki2c_restore,
	.runtime_suspend = quicki2c_runtime_suspend,
	.runtime_resume = quicki2c_runtime_resume,
	.runtime_idle = NULL,
};

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
	.driver.pm = &quicki2c_pm_ops,
	.driver.probe_type = PROBE_PREFER_ASYNCHRONOUS,
};

module_pci_driver(quicki2c_driver);

MODULE_AUTHOR("Xinpeng Sun <xinpeng.sun@intel.com>");
MODULE_AUTHOR("Even Xu <even.xu@intel.com>");

MODULE_DESCRIPTION("Intel(R) QuickI2C Driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("INTEL_THC");
