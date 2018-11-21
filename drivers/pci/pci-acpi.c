// SPDX-License-Identifier: GPL-2.0
/*
 * PCI support in ACPI
 *
 * Copyright (C) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (C) 2004 Tom Long Nguyen <tom.l.nguyen@intel.com>
 * Copyright (C) 2004 Intel Corp.
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/irqdomain.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/pci_hotplug.h>
#include <linux/module.h>
#include <linux/pci-aspm.h>
#include <linux/pci-acpi.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include "pci.h"

/*
 * The GUID is defined in the PCI Firmware Specification available here:
 * https://www.pcisig.com/members/downloads/pcifw_r3_1_13Dec10.pdf
 */
const guid_t pci_acpi_dsm_guid =
	GUID_INIT(0xe5c937d0, 0x3553, 0x4d7a,
		  0x91, 0x17, 0xea, 0x4d, 0x19, 0xc3, 0x43, 0x4d);

#if defined(CONFIG_PCI_QUIRKS) && defined(CONFIG_ARM64)
static int acpi_get_rc_addr(struct acpi_device *adev, struct resource *res)
{
	struct device *dev = &adev->dev;
	struct resource_entry *entry;
	struct list_head list;
	unsigned long flags;
	int ret;

	INIT_LIST_HEAD(&list);
	flags = IORESOURCE_MEM;
	ret = acpi_dev_get_resources(adev, &list,
				     acpi_dev_filter_resource_type_cb,
				     (void *) flags);
	if (ret < 0) {
		dev_err(dev, "failed to parse _CRS method, error code %d\n",
			ret);
		return ret;
	}

	if (ret == 0) {
		dev_err(dev, "no IO and memory resources present in _CRS\n");
		return -EINVAL;
	}

	entry = list_first_entry(&list, struct resource_entry, node);
	*res = *entry->res;
	acpi_dev_free_resource_list(&list);
	return 0;
}

static acpi_status acpi_match_rc(acpi_handle handle, u32 lvl, void *context,
				 void **retval)
{
	u16 *segment = context;
	unsigned long long uid;
	acpi_status status;

	status = acpi_evaluate_integer(handle, "_UID", NULL, &uid);
	if (ACPI_FAILURE(status) || uid != *segment)
		return AE_CTRL_DEPTH;

	*(acpi_handle *)retval = handle;
	return AE_CTRL_TERMINATE;
}

int acpi_get_rc_resources(struct device *dev, const char *hid, u16 segment,
			  struct resource *res)
{
	struct acpi_device *adev;
	acpi_status status;
	acpi_handle handle;
	int ret;

	status = acpi_get_devices(hid, acpi_match_rc, &segment, &handle);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "can't find _HID %s device to locate resources\n",
			hid);
		return -ENODEV;
	}

	ret = acpi_bus_get_device(handle, &adev);
	if (ret)
		return ret;

	ret = acpi_get_rc_addr(adev, res);
	if (ret) {
		dev_err(dev, "can't get resource from %s\n",
			dev_name(&adev->dev));
		return ret;
	}

	return 0;
}
#endif

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

static acpi_status decode_type0_hpx_record(union acpi_object *record,
					   struct hotplug_params *hpx)
{
	int i;
	union acpi_object *fields = record->package.elements;
	u32 revision = fields[1].integer.value;

	switch (revision) {
	case 1:
		if (record->package.count != 6)
			return AE_ERROR;
		for (i = 2; i < 6; i++)
			if (fields[i].type != ACPI_TYPE_INTEGER)
				return AE_ERROR;
		hpx->t0 = &hpx->type0_data;
		hpx->t0->revision        = revision;
		hpx->t0->cache_line_size = fields[2].integer.value;
		hpx->t0->latency_timer   = fields[3].integer.value;
		hpx->t0->enable_serr     = fields[4].integer.value;
		hpx->t0->enable_perr     = fields[5].integer.value;
		break;
	default:
		printk(KERN_WARNING
		       "%s: Type 0 Revision %d record not supported\n",
		       __func__, revision);
		return AE_ERROR;
	}
	return AE_OK;
}

static acpi_status decode_type1_hpx_record(union acpi_object *record,
					   struct hotplug_params *hpx)
{
	int i;
	union acpi_object *fields = record->package.elements;
	u32 revision = fields[1].integer.value;

	switch (revision) {
	case 1:
		if (record->package.count != 5)
			return AE_ERROR;
		for (i = 2; i < 5; i++)
			if (fields[i].type != ACPI_TYPE_INTEGER)
				return AE_ERROR;
		hpx->t1 = &hpx->type1_data;
		hpx->t1->revision      = revision;
		hpx->t1->max_mem_read  = fields[2].integer.value;
		hpx->t1->avg_max_split = fields[3].integer.value;
		hpx->t1->tot_max_split = fields[4].integer.value;
		break;
	default:
		printk(KERN_WARNING
		       "%s: Type 1 Revision %d record not supported\n",
		       __func__, revision);
		return AE_ERROR;
	}
	return AE_OK;
}

static acpi_status decode_type2_hpx_record(union acpi_object *record,
					   struct hotplug_params *hpx)
{
	int i;
	union acpi_object *fields = record->package.elements;
	u32 revision = fields[1].integer.value;

	switch (revision) {
	case 1:
		if (record->package.count != 18)
			return AE_ERROR;
		for (i = 2; i < 18; i++)
			if (fields[i].type != ACPI_TYPE_INTEGER)
				return AE_ERROR;
		hpx->t2 = &hpx->type2_data;
		hpx->t2->revision      = revision;
		hpx->t2->unc_err_mask_and      = fields[2].integer.value;
		hpx->t2->unc_err_mask_or       = fields[3].integer.value;
		hpx->t2->unc_err_sever_and     = fields[4].integer.value;
		hpx->t2->unc_err_sever_or      = fields[5].integer.value;
		hpx->t2->cor_err_mask_and      = fields[6].integer.value;
		hpx->t2->cor_err_mask_or       = fields[7].integer.value;
		hpx->t2->adv_err_cap_and       = fields[8].integer.value;
		hpx->t2->adv_err_cap_or        = fields[9].integer.value;
		hpx->t2->pci_exp_devctl_and    = fields[10].integer.value;
		hpx->t2->pci_exp_devctl_or     = fields[11].integer.value;
		hpx->t2->pci_exp_lnkctl_and    = fields[12].integer.value;
		hpx->t2->pci_exp_lnkctl_or     = fields[13].integer.value;
		hpx->t2->sec_unc_err_sever_and = fields[14].integer.value;
		hpx->t2->sec_unc_err_sever_or  = fields[15].integer.value;
		hpx->t2->sec_unc_err_mask_and  = fields[16].integer.value;
		hpx->t2->sec_unc_err_mask_or   = fields[17].integer.value;
		break;
	default:
		printk(KERN_WARNING
		       "%s: Type 2 Revision %d record not supported\n",
		       __func__, revision);
		return AE_ERROR;
	}
	return AE_OK;
}

static acpi_status acpi_run_hpx(acpi_handle handle, struct hotplug_params *hpx)
{
	acpi_status status;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *package, *record, *fields;
	u32 type;
	int i;

	/* Clear the return buffer with zeros */
	memset(hpx, 0, sizeof(struct hotplug_params));

	status = acpi_evaluate_object(handle, "_HPX", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return status;

	package = (union acpi_object *)buffer.pointer;
	if (package->type != ACPI_TYPE_PACKAGE) {
		status = AE_ERROR;
		goto exit;
	}

	for (i = 0; i < package->package.count; i++) {
		record = &package->package.elements[i];
		if (record->type != ACPI_TYPE_PACKAGE) {
			status = AE_ERROR;
			goto exit;
		}

		fields = record->package.elements;
		if (fields[0].type != ACPI_TYPE_INTEGER ||
		    fields[1].type != ACPI_TYPE_INTEGER) {
			status = AE_ERROR;
			goto exit;
		}

		type = fields[0].integer.value;
		switch (type) {
		case 0:
			status = decode_type0_hpx_record(record, hpx);
			if (ACPI_FAILURE(status))
				goto exit;
			break;
		case 1:
			status = decode_type1_hpx_record(record, hpx);
			if (ACPI_FAILURE(status))
				goto exit;
			break;
		case 2:
			status = decode_type2_hpx_record(record, hpx);
			if (ACPI_FAILURE(status))
				goto exit;
			break;
		default:
			printk(KERN_ERR "%s: Type %d record not supported\n",
			       __func__, type);
			status = AE_ERROR;
			goto exit;
		}
	}
 exit:
	kfree(buffer.pointer);
	return status;
}

static acpi_status acpi_run_hpp(acpi_handle handle, struct hotplug_params *hpp)
{
	acpi_status status;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *package, *fields;
	int i;

	memset(hpp, 0, sizeof(struct hotplug_params));

	status = acpi_evaluate_object(handle, "_HPP", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return status;

	package = (union acpi_object *) buffer.pointer;
	if (package->type != ACPI_TYPE_PACKAGE ||
	    package->package.count != 4) {
		status = AE_ERROR;
		goto exit;
	}

	fields = package->package.elements;
	for (i = 0; i < 4; i++) {
		if (fields[i].type != ACPI_TYPE_INTEGER) {
			status = AE_ERROR;
			goto exit;
		}
	}

	hpp->t0 = &hpp->type0_data;
	hpp->t0->revision        = 1;
	hpp->t0->cache_line_size = fields[0].integer.value;
	hpp->t0->latency_timer   = fields[1].integer.value;
	hpp->t0->enable_serr     = fields[2].integer.value;
	hpp->t0->enable_perr     = fields[3].integer.value;

exit:
	kfree(buffer.pointer);
	return status;
}

/* pci_get_hp_params
 *
 * @dev - the pci_dev for which we want parameters
 * @hpp - allocated by the caller
 */
int pci_get_hp_params(struct pci_dev *dev, struct hotplug_params *hpp)
{
	acpi_status status;
	acpi_handle handle, phandle;
	struct pci_bus *pbus;

	if (acpi_pci_disabled)
		return -ENODEV;

	handle = NULL;
	for (pbus = dev->bus; pbus; pbus = pbus->parent) {
		handle = acpi_pci_get_bridge_handle(pbus);
		if (handle)
			break;
	}

	/*
	 * _HPP settings apply to all child buses, until another _HPP is
	 * encountered. If we don't find an _HPP for the input pci dev,
	 * look for it in the parent device scope since that would apply to
	 * this pci dev.
	 */
	while (handle) {
		status = acpi_run_hpx(handle, hpp);
		if (ACPI_SUCCESS(status))
			return 0;
		status = acpi_run_hpp(handle, hpp);
		if (ACPI_SUCCESS(status))
			return 0;
		if (acpi_is_root_bridge(handle))
			break;
		status = acpi_get_parent(handle, &phandle);
		if (ACPI_FAILURE(status))
			break;
		handle = phandle;
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(pci_get_hp_params);

/**
 * pciehp_is_native - Check whether a hotplug port is handled by the OS
 * @pdev: Hotplug port to check
 *
 * Walk up from @pdev to the host bridge, obtain its cached _OSC Control Field
 * and return the value of the "PCI Express Native Hot Plug control" bit.
 * On failure to obtain the _OSC Control Field return %false.
 */
bool pciehp_is_native(struct pci_dev *pdev)
{
	struct acpi_pci_root *root;
	acpi_handle handle;

	handle = acpi_find_root_bridge_handle(pdev);
	if (!handle)
		return false;

	root = acpi_pci_find_root(handle);
	if (!root)
		return false;

	return root->osc_control_set & OSC_PCI_EXPRESS_NATIVE_HP_CONTROL;
}

/**
 * pci_acpi_wake_bus - Root bus wakeup notification fork function.
 * @context: Device wakeup context.
 */
static void pci_acpi_wake_bus(struct acpi_device_wakeup_context *context)
{
	struct acpi_device *adev;
	struct acpi_pci_root *root;

	adev = container_of(context, struct acpi_device, wakeup.context);
	root = acpi_driver_data(adev);
	pci_pme_wakeup_bus(root->bus);
}

/**
 * pci_acpi_wake_dev - PCI device wakeup notification work function.
 * @context: Device wakeup context.
 */
static void pci_acpi_wake_dev(struct acpi_device_wakeup_context *context)
{
	struct pci_dev *pci_dev;

	pci_dev = to_pci_dev(context->dev);

	if (pci_dev->pme_poll)
		pci_dev->pme_poll = false;

	if (pci_dev->current_state == PCI_D3cold) {
		pci_wakeup_event(pci_dev);
		pm_request_resume(&pci_dev->dev);
		return;
	}

	/* Clear PME Status if set. */
	if (pci_dev->pme_support)
		pci_check_pme_status(pci_dev);

	pci_wakeup_event(pci_dev);
	pm_request_resume(&pci_dev->dev);

	pci_pme_wakeup_bus(pci_dev->subordinate);
}

/**
 * pci_acpi_add_bus_pm_notifier - Register PM notifier for root PCI bus.
 * @dev: PCI root bridge ACPI device.
 */
acpi_status pci_acpi_add_bus_pm_notifier(struct acpi_device *dev)
{
	return acpi_add_pm_notifier(dev, NULL, pci_acpi_wake_bus);
}

/**
 * pci_acpi_add_pm_notifier - Register PM notifier for given PCI device.
 * @dev: ACPI device to add the notifier for.
 * @pci_dev: PCI device to check for the PME status if an event is signaled.
 */
acpi_status pci_acpi_add_pm_notifier(struct acpi_device *dev,
				     struct pci_dev *pci_dev)
{
	return acpi_add_pm_notifier(dev, &pci_dev->dev, pci_acpi_wake_dev);
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
		[PCI_D3hot] = ACPI_STATE_D3_HOT,
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
		pci_dbg(dev, "power state changed by ACPI to %s\n",
			 acpi_power_state_string(state_conv[state]));

	return error;
}

static pci_power_t acpi_pci_get_power_state(struct pci_dev *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->dev);
	static const pci_power_t state_conv[] = {
		[ACPI_STATE_D0]      = PCI_D0,
		[ACPI_STATE_D1]      = PCI_D1,
		[ACPI_STATE_D2]      = PCI_D2,
		[ACPI_STATE_D3_HOT]  = PCI_D3hot,
		[ACPI_STATE_D3_COLD] = PCI_D3cold,
	};
	int state;

	if (!adev || !acpi_device_power_manageable(adev))
		return PCI_UNKNOWN;

	if (acpi_device_get_power(adev, &state) || state == ACPI_STATE_UNKNOWN)
		return PCI_UNKNOWN;

	return state_conv[state];
}

static int acpi_pci_propagate_wakeup(struct pci_bus *bus, bool enable)
{
	while (bus->parent) {
		if (acpi_pm_device_can_wakeup(&bus->self->dev))
			return acpi_pm_set_bridge_wakeup(&bus->self->dev, enable);

		bus = bus->parent;
	}

	/* We have reached the root bus. */
	if (bus->bridge) {
		if (acpi_pm_device_can_wakeup(bus->bridge))
			return acpi_pm_set_bridge_wakeup(bus->bridge, enable);
	}
	return 0;
}

static int acpi_pci_wakeup(struct pci_dev *dev, bool enable)
{
	if (acpi_pm_device_can_wakeup(&dev->dev))
		return acpi_pm_set_device_wakeup(&dev->dev, enable);

	return acpi_pci_propagate_wakeup(dev->bus, enable);
}

static bool acpi_pci_need_resume(struct pci_dev *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->dev);

	if (!adev || !acpi_device_power_manageable(adev))
		return false;

	if (device_may_wakeup(&dev->dev) != !!adev->wakeup.prepare_count)
		return true;

	if (acpi_target_system_state() == ACPI_STATE_S0)
		return false;

	return !!adev->power.flags.dsw_present;
}

static const struct pci_platform_pm_ops acpi_pci_platform_pm = {
	.is_manageable = acpi_pci_power_manageable,
	.set_state = acpi_pci_set_power_state,
	.get_state = acpi_pci_get_power_state,
	.choose_state = acpi_pci_choose_state,
	.set_wakeup = acpi_pci_wakeup,
	.need_resume = acpi_pci_need_resume,
};

void acpi_pci_add_bus(struct pci_bus *bus)
{
	union acpi_object *obj;
	struct pci_host_bridge *bridge;

	if (acpi_pci_disabled || !bus->bridge || !ACPI_HANDLE(bus->bridge))
		return;

	acpi_pci_slot_enumerate(bus);
	acpiphp_enumerate_slots(bus);

	/*
	 * For a host bridge, check its _DSM for function 8 and if
	 * that is available, mark it in pci_host_bridge.
	 */
	if (!pci_is_root_bus(bus))
		return;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(bus->bridge), &pci_acpi_dsm_guid, 3,
				RESET_DELAY_DSM, NULL);
	if (!obj)
		return;

	if (obj->type == ACPI_TYPE_INTEGER && obj->integer.value == 1) {
		bridge = pci_find_host_bridge(bus);
		bridge->ignore_reset_delay = 1;
	}
	ACPI_FREE(obj);
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

/**
 * pci_acpi_optimize_delay - optimize PCI D3 and D3cold delay from ACPI
 * @pdev: the PCI device whose delay is to be updated
 * @handle: ACPI handle of this device
 *
 * Update the d3_delay and d3cold_delay of a PCI device from the ACPI _DSM
 * control method of either the device itself or the PCI host bridge.
 *
 * Function 8, "Reset Delay," applies to the entire hierarchy below a PCI
 * host bridge.  If it returns one, the OS may assume that all devices in
 * the hierarchy have already completed power-on reset delays.
 *
 * Function 9, "Device Readiness Durations," applies only to the object
 * where it is located.  It returns delay durations required after various
 * events if the device requires less time than the spec requires.  Delays
 * from this function take precedence over the Reset Delay function.
 *
 * These _DSM functions are defined by the draft ECN of January 28, 2014,
 * titled "ACPI additions for FW latency optimizations."
 */
static void pci_acpi_optimize_delay(struct pci_dev *pdev,
				    acpi_handle handle)
{
	struct pci_host_bridge *bridge = pci_find_host_bridge(pdev->bus);
	int value;
	union acpi_object *obj, *elements;

	if (bridge->ignore_reset_delay)
		pdev->d3cold_delay = 0;

	obj = acpi_evaluate_dsm(handle, &pci_acpi_dsm_guid, 3,
				FUNCTION_DELAY_DSM, NULL);
	if (!obj)
		return;

	if (obj->type == ACPI_TYPE_PACKAGE && obj->package.count == 5) {
		elements = obj->package.elements;
		if (elements[0].type == ACPI_TYPE_INTEGER) {
			value = (int)elements[0].integer.value / 1000;
			if (value < PCI_PM_D3COLD_WAIT)
				pdev->d3cold_delay = value;
		}
		if (elements[3].type == ACPI_TYPE_INTEGER) {
			value = (int)elements[3].integer.value / 1000;
			if (value < PCI_PM_D3_WAIT)
				pdev->d3_delay = value;
		}
	}
	ACPI_FREE(obj);
}

static void pci_acpi_setup(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct acpi_device *adev = ACPI_COMPANION(dev);

	if (!adev)
		return;

	pci_acpi_optimize_delay(pci_dev, adev->handle);

	pci_acpi_add_pm_notifier(adev, pci_dev);
	if (!adev->wakeup.flags.valid)
		return;

	device_set_wakeup_capable(dev, true);
	acpi_pci_wakeup(pci_dev, false);
}

static void pci_acpi_cleanup(struct device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);

	if (!adev)
		return;

	pci_acpi_remove_pm_notifier(adev);
	if (adev->wakeup.flags.valid)
		device_set_wakeup_capable(dev, false);
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


static struct fwnode_handle *(*pci_msi_get_fwnode_cb)(struct device *dev);

/**
 * pci_msi_register_fwnode_provider - Register callback to retrieve fwnode
 * @fn:       Callback matching a device to a fwnode that identifies a PCI
 *            MSI domain.
 *
 * This should be called by irqchip driver, which is the parent of
 * the MSI domain to provide callback interface to query fwnode.
 */
void
pci_msi_register_fwnode_provider(struct fwnode_handle *(*fn)(struct device *))
{
	pci_msi_get_fwnode_cb = fn;
}

/**
 * pci_host_bridge_acpi_msi_domain - Retrieve MSI domain of a PCI host bridge
 * @bus:      The PCI host bridge bus.
 *
 * This function uses the callback function registered by
 * pci_msi_register_fwnode_provider() to retrieve the irq_domain with
 * type DOMAIN_BUS_PCI_MSI of the specified host bridge bus.
 * This returns NULL on error or when the domain is not found.
 */
struct irq_domain *pci_host_bridge_acpi_msi_domain(struct pci_bus *bus)
{
	struct fwnode_handle *fwnode;

	if (!pci_msi_get_fwnode_cb)
		return NULL;

	fwnode = pci_msi_get_fwnode_cb(&bus->dev);
	if (!fwnode)
		return NULL;

	return irq_find_matching_fwnode(fwnode, DOMAIN_BUS_PCI_MSI);
}

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
