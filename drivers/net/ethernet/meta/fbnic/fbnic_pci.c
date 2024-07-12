// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "fbnic.h"
#include "fbnic_drvinfo.h"

char fbnic_driver_name[] = DRV_NAME;

MODULE_DESCRIPTION(DRV_SUMMARY);
MODULE_LICENSE("GPL");

static const struct fbnic_info fbnic_asic_info = {
	.bar_mask = BIT(0) | BIT(4)
};

static const struct fbnic_info *fbnic_info_tbl[] = {
	[fbnic_board_asic] = &fbnic_asic_info,
};

static const struct pci_device_id fbnic_pci_tbl[] = {
	{ PCI_DEVICE_DATA(META, FBNIC_ASIC, fbnic_board_asic) },
	/* Required last entry */
	{0, }
};
MODULE_DEVICE_TABLE(pci, fbnic_pci_tbl);

u32 fbnic_rd32(struct fbnic_dev *fbd, u32 reg)
{
	u32 __iomem *csr = READ_ONCE(fbd->uc_addr0);
	u32 value;

	if (!csr)
		return ~0U;

	value = readl(csr + reg);

	/* If any bits are 0 value should be valid */
	if (~value)
		return value;

	/* All 1's may be valid if ZEROs register still works */
	if (reg != FBNIC_MASTER_SPARE_0 && ~readl(csr + FBNIC_MASTER_SPARE_0))
		return value;

	/* Hardware is giving us all 1's reads, assume it is gone */
	WRITE_ONCE(fbd->uc_addr0, NULL);
	WRITE_ONCE(fbd->uc_addr4, NULL);

	dev_err(fbd->dev,
		"Failed read (idx 0x%x AKA addr 0x%x), disabled CSR access, awaiting reset\n",
		reg, reg << 2);

	return ~0U;
}

bool fbnic_fw_present(struct fbnic_dev *fbd)
{
	return !!READ_ONCE(fbd->uc_addr4);
}

void fbnic_fw_wr32(struct fbnic_dev *fbd, u32 reg, u32 val)
{
	u32 __iomem *csr = READ_ONCE(fbd->uc_addr4);

	if (csr)
		writel(val, csr + reg);
}

u32 fbnic_fw_rd32(struct fbnic_dev *fbd, u32 reg)
{
	u32 __iomem *csr = READ_ONCE(fbd->uc_addr4);
	u32 value;

	if (!csr)
		return ~0U;

	value = readl(csr + reg);

	/* If any bits are 0 value should be valid */
	if (~value)
		return value;

	/* All 1's may be valid if ZEROs register still works */
	if (reg != FBNIC_FW_ZERO_REG && ~readl(csr + FBNIC_FW_ZERO_REG))
		return value;

	/* Hardware is giving us all 1's reads, assume it is gone */
	WRITE_ONCE(fbd->uc_addr0, NULL);
	WRITE_ONCE(fbd->uc_addr4, NULL);

	dev_err(fbd->dev,
		"Failed read (idx 0x%x AKA addr 0x%x), disabled CSR access, awaiting reset\n",
		reg, reg << 2);

	return ~0U;
}

/**
 * fbnic_probe - Device Initialization Routine
 * @pdev: PCI device information struct
 * @ent: entry in fbnic_pci_tbl
 *
 * Initializes a PCI device identified by a pci_dev structure.
 * The OS initialization, configuring of the adapter private structure,
 * and a hardware reset occur.
 *
 * Return: 0 on success, negative on failure
 **/
static int fbnic_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct fbnic_info *info = fbnic_info_tbl[ent->driver_data];
	struct fbnic_dev *fbd;
	int err;

	if (pdev->error_state != pci_channel_io_normal) {
		dev_err(&pdev->dev,
			"PCI device still in an error state. Unable to load...\n");
		return -EIO;
	}

	err = pcim_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "PCI enable device failed: %d\n", err);
		return err;
	}

	err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(46));
	if (err)
		err = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (err) {
		dev_err(&pdev->dev, "DMA configuration failed: %d\n", err);
		return err;
	}

	err = pcim_iomap_regions(pdev, info->bar_mask, fbnic_driver_name);
	if (err) {
		dev_err(&pdev->dev,
			"pci_request_selected_regions failed: %d\n", err);
		return err;
	}

	fbd = fbnic_devlink_alloc(pdev);
	if (!fbd) {
		dev_err(&pdev->dev, "Devlink allocation failed\n");
		return -ENOMEM;
	}

	pci_set_master(pdev);
	pci_save_state(pdev);

	err = fbnic_alloc_irqs(fbd);
	if (err)
		goto free_fbd;

	err = fbnic_mac_init(fbd);
	if (err) {
		dev_err(&pdev->dev, "Failed to initialize MAC: %d\n", err);
		goto free_irqs;
	}

	err = fbnic_fw_enable_mbx(fbd);
	if (err) {
		dev_err(&pdev->dev,
			"Firmware mailbox initialization failure\n");
		goto free_irqs;
	}

	fbnic_devlink_register(fbd);

	if (!fbd->dsn) {
		dev_warn(&pdev->dev, "Reading serial number failed\n");
		goto init_failure_mode;
	}

	return 0;

init_failure_mode:
	dev_warn(&pdev->dev, "Probe error encountered, entering init failure mode. Normal networking functionality will not be available.\n");
	 /* Always return 0 even on error so devlink is registered to allow
	  * firmware updates for fixes.
	  */
	return 0;
free_irqs:
	fbnic_free_irqs(fbd);
free_fbd:
	pci_disable_device(pdev);
	fbnic_devlink_free(fbd);

	return err;
}

/**
 * fbnic_remove - Device Removal Routine
 * @pdev: PCI device information struct
 *
 * Called by the PCI subsystem to alert the driver that it should release
 * a PCI device.  The could be caused by a Hot-Plug event, or because the
 * driver is going to be removed from memory.
 **/
static void fbnic_remove(struct pci_dev *pdev)
{
	struct fbnic_dev *fbd = pci_get_drvdata(pdev);

	fbnic_devlink_unregister(fbd);
	fbnic_fw_disable_mbx(fbd);
	fbnic_free_irqs(fbd);

	pci_disable_device(pdev);
	fbnic_devlink_free(fbd);
}

static int fbnic_pm_suspend(struct device *dev)
{
	struct fbnic_dev *fbd = dev_get_drvdata(dev);

	fbnic_fw_disable_mbx(fbd);

	/* Free the IRQs so they aren't trying to occupy sleeping CPUs */
	fbnic_free_irqs(fbd);

	/* Hardware is about to go away, so switch off MMIO access internally */
	WRITE_ONCE(fbd->uc_addr0, NULL);
	WRITE_ONCE(fbd->uc_addr4, NULL);

	return 0;
}

static int __fbnic_pm_resume(struct device *dev)
{
	struct fbnic_dev *fbd = dev_get_drvdata(dev);
	void __iomem * const *iomap_table;
	int err;

	/* Restore MMIO access */
	iomap_table = pcim_iomap_table(to_pci_dev(dev));
	fbd->uc_addr0 = iomap_table[0];
	fbd->uc_addr4 = iomap_table[4];

	/* Rerequest the IRQs */
	err = fbnic_alloc_irqs(fbd);
	if (err)
		goto err_invalidate_uc_addr;

	fbd->mac->init_regs(fbd);

	/* Re-enable mailbox */
	err = fbnic_fw_enable_mbx(fbd);
	if (err)
		goto err_free_irqs;

	return 0;
err_free_irqs:
	fbnic_free_irqs(fbd);
err_invalidate_uc_addr:
	WRITE_ONCE(fbd->uc_addr0, NULL);
	WRITE_ONCE(fbd->uc_addr4, NULL);
	return err;
}

static int __maybe_unused fbnic_pm_resume(struct device *dev)
{
	int err;

	err = __fbnic_pm_resume(dev);

	return err;
}

static const struct dev_pm_ops fbnic_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(fbnic_pm_suspend, fbnic_pm_resume)
};

static void fbnic_shutdown(struct pci_dev *pdev)
{
	fbnic_pm_suspend(&pdev->dev);
}

static pci_ers_result_t fbnic_err_error_detected(struct pci_dev *pdev,
						 pci_channel_state_t state)
{
	/* Disconnect device if failure is not recoverable via reset */
	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	fbnic_pm_suspend(&pdev->dev);

	/* Request a slot reset */
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t fbnic_err_slot_reset(struct pci_dev *pdev)
{
	int err;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_save_state(pdev);

	if (pci_enable_device_mem(pdev)) {
		dev_err(&pdev->dev,
			"Cannot re-enable PCI device after reset.\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	/* Restore device to previous state */
	err = __fbnic_pm_resume(&pdev->dev);

	return err ? PCI_ERS_RESULT_DISCONNECT : PCI_ERS_RESULT_RECOVERED;
}

static void fbnic_err_resume(struct pci_dev *pdev)
{
}

static const struct pci_error_handlers fbnic_err_handler = {
	.error_detected	= fbnic_err_error_detected,
	.slot_reset	= fbnic_err_slot_reset,
	.resume		= fbnic_err_resume,
};

static struct pci_driver fbnic_driver = {
	.name		= fbnic_driver_name,
	.id_table	= fbnic_pci_tbl,
	.probe		= fbnic_probe,
	.remove		= fbnic_remove,
	.driver.pm	= &fbnic_pm_ops,
	.shutdown	= fbnic_shutdown,
	.err_handler	= &fbnic_err_handler,
};

/**
 * fbnic_init_module - Driver Registration Routine
 *
 * The first routine called when the driver is loaded.  All it does is
 * register with the PCI subsystem.
 *
 * Return: 0 on success, negative on failure
 **/
static int __init fbnic_init_module(void)
{
	int err;

	err = pci_register_driver(&fbnic_driver);
	if (err)
		goto out;

	pr_info(DRV_SUMMARY " (%s)", fbnic_driver.name);
out:
	return err;
}
module_init(fbnic_init_module);

/**
 * fbnic_exit_module - Driver Exit Cleanup Routine
 *
 * Called just before the driver is removed from memory.
 **/
static void __exit fbnic_exit_module(void)
{
	pci_unregister_driver(&fbnic_driver);
}
module_exit(fbnic_exit_module);
