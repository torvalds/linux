// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <linux/pci.h>

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_pci.h"
#include "xe_pm.h"
#include "xe_printk.h"
#include "xe_survivability_mode.h"

static void prepare_device_for_reset(struct pci_dev *pdev)
{
	struct xe_device *xe = pdev_to_xe_device(pdev);
	struct xe_gt *gt;
	u8 id;

	/*
	 * Wedge the device to prevent userspace access but do not send the uevent.
	 * xe_device_wedged_fini() releases runtime pm if wedged flag is set, so acquire a runtime
	 * pm reference to avoid underflow.
	 */
	if (!atomic_xchg(&xe->wedged.flag, 1))
		xe_pm_runtime_get_noresume(xe);

	xe_device_set_in_reset(xe);

	for_each_gt(gt, xe, id)
		xe_gt_declare_wedged(gt);

	pci_disable_device(pdev);
}

static pci_ers_result_t xe_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	struct xe_device *xe = pdev_to_xe_device(pdev);

	xe_info(xe, "PCI error: detected state = %d\n", state);

	if (state == pci_channel_io_perm_failure)
		return PCI_ERS_RESULT_DISCONNECT;

	/* If the device is already wedged or in survivability mode, do not attempt recovery */
	if (xe_survivability_mode_is_boot_enabled(xe) || xe_device_wedged(xe))
		return PCI_ERS_RESULT_DISCONNECT;

	switch (state) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		prepare_device_for_reset(pdev);
		return PCI_ERS_RESULT_NEED_RESET;
	default:
		xe_info(xe, "PCI error: unknown state %d\n", state);
		return PCI_ERS_RESULT_DISCONNECT;
	}
}

static pci_ers_result_t xe_pci_error_mmio_enabled(struct pci_dev *pdev)
{
	struct xe_device *xe = pdev_to_xe_device(pdev);

	xe_info(xe, "PCI error: MMIO enabled\n");

	/* TODO: Query system controller for the type of error and take appropriate action */
	return PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t xe_pci_error_slot_reset(struct pci_dev *pdev)
{
	const struct pci_device_id *ent = pci_match_id(pdev->driver->id_table, pdev);
	struct xe_device *xe = pdev_to_xe_device(pdev);

	xe_info(xe, "PCI error: slot reset\n");

	pci_restore_state(pdev);

	if (pci_enable_device(pdev)) {
		xe_err(xe, "Cannot re-enable PCI device after reset\n");
		return PCI_ERS_RESULT_DISCONNECT;
	}

	/*
	 * Secondary Bus Reset causes all VRAM state to be lost along with
	 * hardware state. As an initial step, re-probe the device to
	 * re-initialize the driver and hardware.
	 * TODO: optimize by re-initializing only the hardware state and re-creating
	 * kernel BOs.
	 */
	xe_device_clear_in_reset(xe);
	pdev->driver->remove(pdev);
	devres_release_group(&pdev->dev, xe->devres_group);

	if (pdev->driver->probe(pdev, ent))
		return PCI_ERS_RESULT_DISCONNECT;

	xe = pdev_to_xe_device(pdev);

	/* Wedge the device to prevent I/O operations till the resume callback */
	atomic_set(&xe->wedged.flag, 1);

	return PCI_ERS_RESULT_RECOVERED;
}

static void xe_pci_error_resume(struct pci_dev *pdev)
{
	struct xe_device *xe = pdev_to_xe_device(pdev);

	xe_info(xe, "PCI error: resume\n");

	atomic_set(&xe->wedged.flag, 0);
}

const struct pci_error_handlers xe_pci_error_handlers = {
	.error_detected	= xe_pci_error_detected,
	.mmio_enabled	= xe_pci_error_mmio_enabled,
	.slot_reset	= xe_pci_error_slot_reset,
	.resume		= xe_pci_error_resume,
};
