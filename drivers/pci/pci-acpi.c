/*
 * File:	pci-acpi.c
 * Purpose:	Provide PCI support in ACPI
 *
 * Copyright (C) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (C) 2004 Tom Long Nguyen <tom.l.nguyen@intel.com>
 * Copyright (C) 2004 Intel Corp.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/pci-aspm.h>
#include <linux/pci-acpi.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include "pci.h"

/**
 * pci_acpi_wake_bus - Wake-up notification handler for root buses.
 * @handle: ACPI handle of a device the notification is for.
 * @event: Type of the signaled event.
 * @context: PCI root bus to wake up devices on.
 */
static void pci_acpi_wake_bus(acpi_handle handle, u32 event, void *context)
{
	struct pci_bus *pci_bus = context;

	if (event == ACPI_NOTIFY_DEVICE_WAKE && pci_bus)
		pci_pme_wakeup_bus(pci_bus);
}

/**
 * pci_acpi_wake_dev - Wake-up notification handler for PCI devices.
 * @handle: ACPI handle of a device the notification is for.
 * @event: Type of the signaled event.
 * @context: PCI device object to wake up.
 */
static void pci_acpi_wake_dev(acpi_handle handle, u32 event, void *context)
{
	struct pci_dev *pci_dev = context;

	if (event != ACPI_NOTIFY_DEVICE_WAKE || !pci_dev)
		return;

	if (pci_dev->pme_poll)
		pci_dev->pme_poll = false;

	if (pci_dev->current_state == PCI_D3cold) {
		pci_wakeup_event(pci_dev);
		pm_runtime_resume(&pci_dev->dev);
		return;
	}

	/* Clear PME Status if set. */
	if (pci_dev->pme_support)
		pci_check_pme_status(pci_dev);

	pci_wakeup_event(pci_dev);
	pm_runtime_resume(&pci_dev->dev);

	if (pci_dev->subordinate)
		pci_pme_wakeup_bus(pci_dev->subordinate);
}

/**
 * pci_acpi_add_bus_pm_notifier - Register PM notifier for given PCI bus.
 * @dev: ACPI device to add the notifier for.
 * @pci_bus: PCI bus to walk checking for PME status if an event is signaled.
 */
acpi_status pci_acpi_add_bus_pm_notifier(struct acpi_device *dev,
					 struct pci_bus *pci_bus)
{
	return acpi_add_pm_notifier(dev, pci_acpi_wake_bus, pci_bus);
}

/**
 * pci_acpi_remove_bus_pm_notifier - Unregister PCI bus PM notifier.
 * @dev: ACPI device to remove the notifier from.
 */
acpi_status pci_acpi_remove_bus_pm_notifier(struct acpi_device *dev)
{
	return acpi_remove_pm_notifier(dev, pci_acpi_wake_bus);
}

/**
 * pci_acpi_add_pm_notifier - Register PM notifier for given PCI device.
 * @dev: ACPI device to add the notifier for.
 * @pci_dev: PCI device to check for the PME status if an event is signaled.
 */
acpi_status pci_acpi_add_pm_notifier(struct acpi_device *dev,
				     struct pci_dev *pci_dev)
{
	return acpi_add_pm_notifier(dev, pci_acpi_wake_dev, pci_dev);
}

/**
 * pci_acpi_remove_pm_notifier - Unregister PCI device PM notifier.
 * @dev: ACPI device to remove the notifier from.
 */
acpi_status pci_acpi_remove_pm_notifier(struct acpi_device *dev)
{
	return acpi_remove_pm_notifier(dev, pci_acpi_wake_dev);
}

phys_addr_t acpi_pci_root_get_mcfg_addr(acpi_handle handle)
{
	acpi_status status = AE_NOT_EXIST;
	unsigned long long mcfg_addr;

	if (handle)
		status = acpi_evaluate_integer(handle, METHOD_NAME__CBA,
					       NULL, &mcfg_addr);
	if (ACPI_FAILURE(status))
		return 0;

	return (phys_addr_t)mcfg_addr;
}

/*
 * _SxD returns the D-state with the highest power
 * (lowest D-state number) supported in the S-state "x".
 *
 * If the devices does not have a _PRW
 * (Power Resources for Wake) supporting system wakeup from "x"
 * then the OS is free to choose a lower power (higher number
 * D-state) than the return value from _SxD.
 *
 * But if _PRW is enabled at S-state "x", the OS
 * must not choose a power lower than _SxD --
 * unless the device has an _SxW method specifying
 * the lowest power (highest D-state number) the device
 * may enter while still able to wake the system.
 *
 * ie. depending on global OS policy:
 *
 * if (_PRW at S-state x)
 *	choose from highest power _SxD to lowest power _SxW
 * else // no _PRW at S-state x
 *	choose highest power _SxD or any lower power
 */

static pci_power_t acpi_pci_choose_state(struct pci_dev *pdev)
{
	int acpi_state, d_max;

	if (pdev->no_d3cold)
		d_max = ACPI_STATE_D3_HOT;
	else
		d_max = ACPI_STATE_D3_COLD;
	acpi_state = acpi_pm_device_sleep_state(&pdev->dev, NULL, d_max);
	if (acpi_state < 0)
		return PCI_POWER_ERROR;

	switch (acpi_state) {
	case ACPI_STATE_D0:
		return PCI_D0;
	case ACPI_STATE_D1:
		return PCI_D1;
	case ACPI_STATE_D2:
		return PCI_D2;
	case ACPI_STATE_D3_HOT:
		return PCI_D3hot;
	case ACPI_STATE_D3_COLD:
		return PCI_D3cold;
	}
	return PCI_POWER_ERROR;
}

static bool acpi_pci_power_manageable(struct pci_dev *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->dev);
	return adev ? acpi_device_power_manageable(adev) : false;
}

static int acpi_pci_set_power_state(struct pci_dev *dev, pci_power_t state)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->dev);
	static const u8 state_conv[] = {
		[PCI_D0] = ACPI_STATE_D0,
		[PCI_D1] = ACPI_STATE_D1,
		[PCI_D2] = ACPI_STATE_D2,
		[PCI_D3hot] = ACPI_STATE_D3_COLD,
		[PCI_D3cold] = ACPI_STATE_D3_COLD,
	};
	int error = -EINVAL;

	/* If the ACPI device has _EJ0, ignore the device */
	if (!adev || acpi_has_method(adev->handle, "_EJ0"))
		return -ENODEV;

	switch (state) {
	case PCI_D3cold:
		if (dev_pm_qos_flags(&dev->dev, PM_QOS_FLAG_NO_POWER_OFF) ==
				PM_QOS_FLAGS_ALL) {
			error = -EBUSY;
			break;
		}
	case PCI_D0:
	case PCI_D1:
	case PCI_D2:
	case PCI_D3hot:
		error = acpi_device_set_power(adev, state_conv[state]);
	}

	if (!error)
		dev_dbg(&dev->dev, "power state changed by ACPI to %s\n",
			 acpi_power_state_string(state_conv[state]));

	return error;
}

static bool acpi_pci_can_wakeup(struct pci_dev *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->dev);
	return adev ? acpi_device_can_wakeup(adev) : false;
}

static void acpi_pci_propagate_wakeup_enable(struct pci_bus *bus, bool enable)
{
	while (bus->parent) {
		if (!acpi_pm_device_sleep_wake(&bus->self->dev, enable))
			return;
		bus = bus->parent;
	}

	/* We have reached the root bus. */
	if (bus->bridge)
		acpi_pm_device_sleep_wake(bus->bridge, enable);
}

static int acpi_pci_sleep_wake(struct pci_dev *dev, bool enable)
{
	if (acpi_pci_can_wakeup(dev))
		return acpi_pm_device_sleep_wake(&dev->dev, enable);

	acpi_pci_propagate_wakeup_enable(dev->bus, enable);
	return 0;
}

static void acpi_pci_propagate_run_wake(struct pci_bus *bus, bool enable)
{
	while (bus->parent) {
		struct pci_dev *bridge = bus->self;

		if (bridge->pme_interrupt)
			return;
		if (!acpi_pm_device_run_wake(&bridge->dev, enable))
			return;
		bus = bus->parent;
	}

	/* We have reached the root bus. */
	if (bus->bridge)
		acpi_pm_device_run_wake(bus->bridge, enable);
}

static int acpi_pci_run_wake(struct pci_dev *dev, bool enable)
{
	/*
	 * Per PCI Express Base Specification Revision 2.0 section
	 * 5.3.3.2 Link Wakeup, platform support is needed for D3cold
	 * waking up to power on the main link even if there is PME
	 * support for D3cold
	 */
	if (dev->pme_interrupt && !dev->runtime_d3cold)
		return 0;

	if (!acpi_pm_device_run_wake(&dev->dev, enable))
		return 0;

	acpi_pci_propagate_run_wake(dev->bus, enable);
	return 0;
}

static struct pci_platform_pm_ops acpi_pci_platform_pm = {
	.is_manageable = acpi_pci_power_manageable,
	.set_state = acpi_pci_set_power_state,
	.choose_state = acpi_pci_choose_state,
	.sleep_wake = acpi_pci_sleep_wake,
	.run_wake = acpi_pci_run_wake,
};

void acpi_pci_add_bus(struct pci_bus *bus)
{
	if (acpi_pci_disabled || !bus->bridge)
		return;

	acpi_pci_slot_enumerate(bus);
	acpiphp_enumerate_slots(bus);
}

void acpi_pci_remove_bus(struct pci_bus *bus)
{
	if (acpi_pci_disabled || !bus->bridge)
		return;

	acpiphp_remove_slots(bus);
	acpi_pci_slot_remove(bus);
}

/* ACPI bus type */
static struct acpi_device *acpi_pci_find_companion(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	bool check_children;
	u64 addr;

	check_children = pci_is_bridge(pci_dev);
	/* Please ref to ACPI spec for the syntax of _ADR */
	addr = (PCI_SLOT(pci_dev->devfn) << 16) | PCI_FUNC(pci_dev->devfn);
	return acpi_find_child_device(ACPI_COMPANION(dev->parent), addr,
				      check_children);
}

static void pci_acpi_setup(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct acpi_device *adev = ACPI_COMPANION(dev);

	if (!adev)
		return;

	pci_acpi_add_pm_notifier(adev, pci_dev);
	if (!adev->wakeup.flags.valid)
		return;

	device_set_wakeup_capable(dev, true);
	acpi_pci_sleep_wake(pci_dev, false);
	if (adev->wakeup.flags.run_wake)
		device_set_run_wake(dev, true);
}

static void pci_acpi_cleanup(struct device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);

	if (!adev)
		return;

	pci_acpi_remove_pm_notifier(adev);
	if (adev->wakeup.flags.valid) {
		device_set_wakeup_capable(dev, false);
		device_set_run_wake(dev, false);
	}
}

static bool pci_acpi_bus_match(struct device *dev)
{
	return dev_is_pci(dev);
}

static struct acpi_bus_type acpi_pci_bus = {
	.name = "PCI",
	.match = pci_acpi_bus_match,
	.find_companion = acpi_pci_find_companion,
	.setup = pci_acpi_setup,
	.cleanup = pci_acpi_cleanup,
};

static int __init acpi_pci_init(void)
{
	int ret;

	if (acpi_gbl_FADT.boot_flags & ACPI_FADT_NO_MSI) {
		pr_info("ACPI FADT declares the system doesn't support MSI, so disable it\n");
		pci_no_msi();
	}

	if (acpi_gbl_FADT.boot_flags & ACPI_FADT_NO_ASPM) {
		pr_info("ACPI FADT declares the system doesn't support PCIe ASPM, so disable it\n");
		pcie_no_aspm();
	}

	ret = register_acpi_bus_type(&acpi_pci_bus);
	if (ret)
		return 0;

	pci_set_platform_pm(&acpi_pci_platform_pm);
	acpi_pci_slot_init();
	acpiphp_init();

	return 0;
}
arch_initcall(acpi_pci_init);
