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
#include <acpi/acpi.h>
#include <acpi/acnamesp.h>
#include <acpi/acresrc.h>
#include <acpi/acpi_bus.h>

#include <linux/pci-acpi.h>
#include "pci.h"

struct acpi_osc_data {
	acpi_handle handle;
	u32 support_set;
	u32 control_set;
	int is_queried;
	u32 query_result;
	struct list_head sibiling;
};
static LIST_HEAD(acpi_osc_data_list);

struct acpi_osc_args {
	u32 capbuf[3];
	u32 query_result;
};

static struct acpi_osc_data *acpi_get_osc_data(acpi_handle handle)
{
	struct acpi_osc_data *data;

	list_for_each_entry(data, &acpi_osc_data_list, sibiling) {
		if (data->handle == handle)
			return data;
	}
	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;
	INIT_LIST_HEAD(&data->sibiling);
	data->handle = handle;
	list_add_tail(&data->sibiling, &acpi_osc_data_list);
	return data;
}

static u8 OSC_UUID[16] = {0x5B, 0x4D, 0xDB, 0x33, 0xF7, 0x1F, 0x1C, 0x40,
			  0x96, 0x57, 0x74, 0x41, 0xC0, 0x3D, 0xD7, 0x66};

static acpi_status acpi_run_osc(acpi_handle handle,
				struct acpi_osc_args *osc_args)
{
	acpi_status status;
	struct acpi_object_list input;
	union acpi_object in_params[4];
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *out_obj;
	u32 osc_dw0, flags = osc_args->capbuf[OSC_QUERY_TYPE];

	/* Setting up input parameters */
	input.count = 4;
	input.pointer = in_params;
	in_params[0].type 		= ACPI_TYPE_BUFFER;
	in_params[0].buffer.length 	= 16;
	in_params[0].buffer.pointer	= OSC_UUID;
	in_params[1].type 		= ACPI_TYPE_INTEGER;
	in_params[1].integer.value 	= 1;
	in_params[2].type 		= ACPI_TYPE_INTEGER;
	in_params[2].integer.value	= 3;
	in_params[3].type		= ACPI_TYPE_BUFFER;
	in_params[3].buffer.length 	= 12;
	in_params[3].buffer.pointer 	= (u8 *)osc_args->capbuf;

	status = acpi_evaluate_object(handle, "_OSC", &input, &output);
	if (ACPI_FAILURE(status))
		return status;

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER) {
		printk(KERN_DEBUG "Evaluate _OSC returns wrong type\n");
		status = AE_TYPE;
		goto out_kfree;
	}
	osc_dw0 = *((u32 *)out_obj->buffer.pointer);
	if (osc_dw0) {
		if (osc_dw0 & OSC_REQUEST_ERROR)
			printk(KERN_DEBUG "_OSC request fails\n"); 
		if (osc_dw0 & OSC_INVALID_UUID_ERROR)
			printk(KERN_DEBUG "_OSC invalid UUID\n"); 
		if (osc_dw0 & OSC_INVALID_REVISION_ERROR)
			printk(KERN_DEBUG "_OSC invalid revision\n"); 
		if (osc_dw0 & OSC_CAPABILITIES_MASK_ERROR) {
			if (flags & OSC_QUERY_ENABLE)
				goto out_success;
			printk(KERN_DEBUG "_OSC FW not grant req. control\n");
			status = AE_SUPPORT;
			goto out_kfree;
		}
		status = AE_ERROR;
		goto out_kfree;
	}
out_success:
	if (flags & OSC_QUERY_ENABLE)
		osc_args->query_result =
			*((u32 *)(out_obj->buffer.pointer + 8));
	status = AE_OK;

out_kfree:
	kfree(output.pointer);
	return status;
}

static acpi_status acpi_query_osc(acpi_handle handle,
				  u32 level, void *context, void **retval)
{
	acpi_status status;
	struct acpi_osc_data *osc_data;
	u32 flags = (unsigned long)context, support_set;
	acpi_handle tmp;
	struct acpi_osc_args osc_args;

	status = acpi_get_handle(handle, "_OSC", &tmp);
	if (ACPI_FAILURE(status))
		return status;

	osc_data = acpi_get_osc_data(handle);
	if (!osc_data) {
		printk(KERN_ERR "acpi osc data array is full\n");
		return AE_ERROR;
	}

	/* do _OSC query for all possible controls */
	support_set = osc_data->support_set | (flags & OSC_SUPPORT_MASKS);
	osc_args.capbuf[OSC_QUERY_TYPE] = OSC_QUERY_ENABLE;
	osc_args.capbuf[OSC_SUPPORT_TYPE] = support_set;
	osc_args.capbuf[OSC_CONTROL_TYPE] = OSC_CONTROL_MASKS;

	status = acpi_run_osc(handle, &osc_args);
	if (ACPI_SUCCESS(status)) {
		osc_data->support_set = support_set;
		osc_data->query_result = osc_args.query_result;
		osc_data->is_queried = 1;
	}

	return status;
}

/**
 * __pci_osc_support_set - register OS support to Firmware
 * @flags: OS support bits
 * @hid: hardware ID
 *
 * Update OS support fields and doing a _OSC Query to obtain an update
 * from Firmware on supported control bits.
 **/
acpi_status __pci_osc_support_set(u32 flags, const char *hid)
{
	if (!(flags & OSC_SUPPORT_MASKS))
		return AE_TYPE;

	acpi_get_devices(hid, acpi_query_osc,
			 (void *)(unsigned long)flags, NULL);
	return AE_OK;
}

/**
 * pci_osc_control_set - commit requested control to Firmware
 * @handle: acpi_handle for the target ACPI object
 * @flags: driver's requested control bits
 *
 * Attempt to take control from Firmware on requested control bits.
 **/
acpi_status pci_osc_control_set(acpi_handle handle, u32 flags)
{
	acpi_status status;
	u32 ctrlset, control_set;
	acpi_handle tmp;
	struct acpi_osc_data *osc_data;
	struct acpi_osc_args osc_args;

	status = acpi_get_handle(handle, "_OSC", &tmp);
	if (ACPI_FAILURE(status))
		return status;

	osc_data = acpi_get_osc_data(handle);
	if (!osc_data) {
		printk(KERN_ERR "acpi osc data array is full\n");
		return AE_ERROR;
	}

	ctrlset = (flags & OSC_CONTROL_MASKS);
	if (!ctrlset)
		return AE_TYPE;

	if (osc_data->is_queried &&
	    ((osc_data->query_result & ctrlset) != ctrlset))
		return AE_SUPPORT;

	control_set = osc_data->control_set | ctrlset;
	osc_args.capbuf[OSC_QUERY_TYPE] = 0;
	osc_args.capbuf[OSC_SUPPORT_TYPE] = osc_data->support_set;
	osc_args.capbuf[OSC_CONTROL_TYPE] = control_set;
	status = acpi_run_osc(handle, &osc_args);
	if (ACPI_SUCCESS(status))
		osc_data->control_set = control_set;

	return status;
}
EXPORT_SYMBOL(pci_osc_control_set);

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
 * 	choose highest power _SxD or any lower power
 */

static pci_power_t acpi_pci_choose_state(struct pci_dev *pdev)
{
	int acpi_state;

	acpi_state = acpi_pm_device_sleep_state(&pdev->dev, NULL);
	if (acpi_state < 0)
		return PCI_POWER_ERROR;

	switch (acpi_state) {
	case ACPI_STATE_D0:
		return PCI_D0;
	case ACPI_STATE_D1:
		return PCI_D1;
	case ACPI_STATE_D2:
		return PCI_D2;
	case ACPI_STATE_D3:
		return PCI_D3hot;
	}
	return PCI_POWER_ERROR;
}

static bool acpi_pci_power_manageable(struct pci_dev *dev)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(&dev->dev);

	return handle ? acpi_bus_power_manageable(handle) : false;
}

static int acpi_pci_set_power_state(struct pci_dev *dev, pci_power_t state)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(&dev->dev);
	acpi_handle tmp;
	static const u8 state_conv[] = {
		[PCI_D0] = ACPI_STATE_D0,
		[PCI_D1] = ACPI_STATE_D1,
		[PCI_D2] = ACPI_STATE_D2,
		[PCI_D3hot] = ACPI_STATE_D3,
		[PCI_D3cold] = ACPI_STATE_D3
	};
	int error = -EINVAL;

	/* If the ACPI device has _EJ0, ignore the device */
	if (!handle || ACPI_SUCCESS(acpi_get_handle(handle, "_EJ0", &tmp)))
		return -ENODEV;

	switch (state) {
	case PCI_D0:
	case PCI_D1:
	case PCI_D2:
	case PCI_D3hot:
	case PCI_D3cold:
		error = acpi_bus_set_power(handle, state_conv[state]);
	}

	if (!error)
		dev_printk(KERN_INFO, &dev->dev,
				"power state changed by ACPI to D%d\n", state);

	return error;
}

static bool acpi_pci_can_wakeup(struct pci_dev *dev)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(&dev->dev);

	return handle ? acpi_bus_can_wakeup(handle) : false;
}

static int acpi_pci_sleep_wake(struct pci_dev *dev, bool enable)
{
	int error = acpi_pm_device_sleep_wake(&dev->dev, enable);

	if (!error)
		dev_printk(KERN_INFO, &dev->dev,
				"wake-up capability %s by ACPI\n",
				enable ? "enabled" : "disabled");
	return error;
}

static struct pci_platform_pm_ops acpi_pci_platform_pm = {
	.is_manageable = acpi_pci_power_manageable,
	.set_state = acpi_pci_set_power_state,
	.choose_state = acpi_pci_choose_state,
	.can_wakeup = acpi_pci_can_wakeup,
	.sleep_wake = acpi_pci_sleep_wake,
};

/* ACPI bus type */
static int acpi_pci_find_device(struct device *dev, acpi_handle *handle)
{
	struct pci_dev * pci_dev;
	acpi_integer	addr;

	pci_dev = to_pci_dev(dev);
	/* Please ref to ACPI spec for the syntax of _ADR */
	addr = (PCI_SLOT(pci_dev->devfn) << 16) | PCI_FUNC(pci_dev->devfn);
	*handle = acpi_get_child(DEVICE_ACPI_HANDLE(dev->parent), addr);
	if (!*handle)
		return -ENODEV;
	return 0;
}

static int acpi_pci_find_root_bridge(struct device *dev, acpi_handle *handle)
{
	int num;
	unsigned int seg, bus;

	/*
	 * The string should be the same as root bridge's name
	 * Please look at 'pci_scan_bus_parented'
	 */
	num = sscanf(dev->bus_id, "pci%04x:%02x", &seg, &bus);
	if (num != 2)
		return -ENODEV;
	*handle = acpi_get_pci_rootbridge_handle(seg, bus);
	if (!*handle)
		return -ENODEV;
	return 0;
}

static struct acpi_bus_type acpi_pci_bus = {
	.bus = &pci_bus_type,
	.find_device = acpi_pci_find_device,
	.find_bridge = acpi_pci_find_root_bridge,
};

static int __init acpi_pci_init(void)
{
	int ret;

	if (acpi_gbl_FADT.boot_flags & BAF_MSI_NOT_SUPPORTED) {
		printk(KERN_INFO"ACPI FADT declares the system doesn't support MSI, so disable it\n");
		pci_no_msi();
	}

	if (acpi_gbl_FADT.boot_flags & BAF_PCIE_ASPM_CONTROL) {
		printk(KERN_INFO"ACPI FADT declares the system doesn't support PCIe ASPM, so disable it\n");
		pcie_no_aspm();
	}

	ret = register_acpi_bus_type(&acpi_pci_bus);
	if (ret)
		return 0;
	pci_set_platform_pm(&acpi_pci_platform_pm);
	return 0;
}
arch_initcall(acpi_pci_init);
